/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongerdb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */


#include "monger_embedded/monger_embedded.h"

#include <memory>
#include <set>
#include <yaml-cpp/yaml.h>

#include "monger/base/initializer.h"
#include "monger/bson/bsonobjbuilder.h"
#include "monger/db/commands/test_commands_enabled.h"
#include "monger/db/json.h"
#include "monger/db/server_options.h"
#include "monger/db/storage/mobile/mobile_options.h"
#include "monger/embedded/monger_embedded/monger_embedded_test_gen.h"
#include "monger/rpc/message.h"
#include "monger/rpc/op_msg.h"
#include "monger/stdx/thread.h"
#include "monger/unittest/temp_dir.h"
#include "monger/unittest/unittest.h"
#include "monger/util/options_parser/environment.h"
#include "monger/util/options_parser/option_section.h"
#include "monger/util/options_parser/options_parser.h"
#include "monger/util/quick_exit.h"
#include "monger/util/shared_buffer.h"
#include "monger/util/signal_handlers_synchronous.h"

namespace moe = monger::optionenvironment;

monger_embedded_v1_lib* global_lib_handle;

namespace {

std::unique_ptr<monger::unittest::TempDir> globalTempDir;

struct StatusDestructor {
    void operator()(monger_embedded_v1_status* const p) const noexcept {
        if (p)
            monger_embedded_v1_status_destroy(p);
    }
};

using CapiStatusPtr = std::unique_ptr<monger_embedded_v1_status, StatusDestructor>;

CapiStatusPtr makeStatusPtr() {
    return CapiStatusPtr{monger_embedded_v1_status_create()};
}

struct ClientDestructor {
    void operator()(monger_embedded_v1_client* const p) const noexcept {
        if (!p)
            return;

        auto status = makeStatusPtr();
        if (monger_embedded_v1_client_destroy(p, status.get()) != MONGO_EMBEDDED_V1_SUCCESS) {
            std::cerr << "libmongerdb_capi_client_destroy failed." << std::endl;
            if (status) {
                std::cerr << "Error code: " << monger_embedded_v1_status_get_error(status.get())
                          << std::endl;
                std::cerr << "Error message: "
                          << monger_embedded_v1_status_get_explanation(status.get()) << std::endl;
            }
        }
    }
};

using MongerDBCAPIClientPtr = std::unique_ptr<monger_embedded_v1_client, ClientDestructor>;

class MongerdbCAPITest : public monger::unittest::Test {
protected:
    void setUp() {
        status = monger_embedded_v1_status_create();
        ASSERT(status != nullptr);

        if (!globalTempDir) {
            globalTempDir = std::make_unique<monger::unittest::TempDir>("embedded_monger");
        }

        monger_embedded_v1_init_params params;
        params.log_flags = MONGO_EMBEDDED_V1_LOG_STDOUT;
        params.log_callback = nullptr;
        params.log_user_data = nullptr;

        // Set a parameter that lives in mobileGlobalOptions to verify it can be set using YAML.
        uint32_t vacuumCheckIntervalMinutes = 1;

        YAML::Emitter yaml;
        yaml << YAML::BeginMap;

        yaml << YAML::Key << "storage";
        yaml << YAML::Value << YAML::BeginMap;
        yaml << YAML::Key << "dbPath";
        yaml << YAML::Value << globalTempDir->path();
        yaml << YAML::Key << "mobile" << YAML::BeginMap;
        yaml << YAML::Key << "vacuumCheckIntervalMinutes" << YAML::Value
             << vacuumCheckIntervalMinutes;
        yaml << YAML::EndMap;  // mobile
        yaml << YAML::EndMap;  // storage

        yaml << YAML::EndMap;

        params.yaml_config = yaml.c_str();

        lib = monger_embedded_v1_lib_init(&params, status);
        ASSERT(lib != nullptr) << monger_embedded_v1_status_get_explanation(status);

        db = monger_embedded_v1_instance_create(lib, yaml.c_str(), status);
        ASSERT(db != nullptr) << monger_embedded_v1_status_get_explanation(status);
        ASSERT(monger::embedded::mobileGlobalOptions.vacuumCheckIntervalMinutes ==
               vacuumCheckIntervalMinutes);
    }

    void tearDown() {
        ASSERT_EQUALS(monger_embedded_v1_instance_destroy(db, status), MONGO_EMBEDDED_V1_SUCCESS)
            << monger_embedded_v1_status_get_explanation(status);
        ASSERT_EQUALS(monger_embedded_v1_lib_fini(lib, status), MONGO_EMBEDDED_V1_SUCCESS)
            << monger_embedded_v1_status_get_explanation(status);
        monger_embedded_v1_status_destroy(status);
    }

    monger_embedded_v1_instance* getDB() const {
        return db;
    }

    MongerDBCAPIClientPtr createClient() const {
        MongerDBCAPIClientPtr client(monger_embedded_v1_client_create(db, status));
        ASSERT(client.get() != nullptr) << monger_embedded_v1_status_get_explanation(status);
        return client;
    }

    monger::Message messageFromBuffer(void* data, size_t dataLen) {
        auto sb = monger::SharedBuffer::allocate(dataLen);
        memcpy(sb.get(), data, dataLen);
        monger::Message msg(std::move(sb));
        return msg;
    }

    monger::BSONObj performRpc(MongerDBCAPIClientPtr& client, monger::OpMsgRequest request) {
        auto inputMessage = request.serialize();

        // declare the output size and pointer
        void* output;
        size_t outputSize;

        // call the wire protocol
        int err = monger_embedded_v1_client_invoke(
            client.get(), inputMessage.buf(), inputMessage.size(), &output, &outputSize, status);
        ASSERT_EQUALS(err, MONGO_EMBEDDED_V1_SUCCESS);

        // convert the shared buffer to a monger::message and ensure that it is valid
        auto outputMessage = messageFromBuffer(output, outputSize);
        ASSERT(outputMessage.size() > 0);
        ASSERT(outputMessage.operation() == inputMessage.operation());

        // convert the message into an OpMessage to examine its BSON
        auto outputOpMsg = monger::OpMsg::parseOwned(outputMessage);
        ASSERT(outputOpMsg.body.valid(monger::BSONVersion::kLatest));
        return outputOpMsg.body;
    }


protected:
    monger_embedded_v1_lib* lib;
    monger_embedded_v1_instance* db;
    monger_embedded_v1_status* status;
};

TEST_F(MongerdbCAPITest, CreateAndDestroyDB) {
    // Test the setUp() and tearDown() test fixtures
}

TEST_F(MongerdbCAPITest, CreateAndDestroyDBAndClient) {
    auto client = createClient();
}

// This test is to make sure that destroying the db will fail if there's remaining clients left.
TEST_F(MongerdbCAPITest, DoNotDestroyClient) {
    auto client = createClient();
    ASSERT(monger_embedded_v1_instance_destroy(getDB(), nullptr) != MONGO_EMBEDDED_V1_SUCCESS);
}

TEST_F(MongerdbCAPITest, CreateMultipleClients) {
    const int numClients = 10;
    std::set<MongerDBCAPIClientPtr> clients;
    for (int i = 0; i < numClients; i++) {
        clients.insert(createClient());
    }

    // ensure that each client is unique by making sure that the set size equals the number of
    // clients instantiated
    ASSERT_EQUALS(static_cast<int>(clients.size()), numClients);
}

TEST_F(MongerdbCAPITest, IsMaster) {
    // create the client object
    auto client = createClient();

    // craft the isMaster message
    monger::BSONObj inputObj = monger::fromjson("{isMaster: 1}");
    auto inputOpMsg = monger::OpMsgRequest::fromDBAndBody("admin", inputObj);
    auto output = performRpc(client, inputOpMsg);
    ASSERT(output.getBoolField("ismaster"));
}

TEST_F(MongerdbCAPITest, CreateIndex) {
    // create the client object
    auto client = createClient();

    // craft the createIndexes message
    monger::BSONObj inputObj = monger::fromjson(
        R"raw_delimiter({
            createIndexes: 'items',
            indexes: 
            [
                {
                    key: {
                        task: 1
                    },
                    name: 'task_1'
                }
            ]
        })raw_delimiter");
    auto inputOpMsg = monger::OpMsgRequest::fromDBAndBody("index_db", inputObj);
    auto output = performRpc(client, inputOpMsg);

    ASSERT(output.hasField("ok")) << output;
    ASSERT(output.getField("ok").numberDouble() == 1.0) << output;
    ASSERT(output.getIntField("numIndexesAfter") == output.getIntField("numIndexesBefore") + 1)
        << output;
}

TEST_F(MongerdbCAPITest, CreateBackgroundIndex) {
    // create the client object
    auto client = createClient();

    // craft the createIndexes message
    monger::BSONObj inputObj = monger::fromjson(
        R"raw_delimiter({
            createIndexes: 'items',
            indexes: 
            [
                {
                    key: {
                        task: 1
                    },
                    name: 'task_1',
                    background: true
                }
            ]
        })raw_delimiter");
    auto inputOpMsg = monger::OpMsgRequest::fromDBAndBody("background_index_db", inputObj);
    auto output = performRpc(client, inputOpMsg);

    ASSERT(output.hasField("ok")) << output;
    ASSERT(output.getField("ok").numberDouble() != 1.0) << output;
}

TEST_F(MongerdbCAPITest, CreateTTLIndex) {
    // create the client object
    auto client = createClient();

    // craft the createIndexes message
    monger::BSONObj inputObj = monger::fromjson(
        R"raw_delimiter({
            createIndexes: 'items',
            indexes: 
            [
                {
                    key: {
                        task: 1
                    },
                    name: 'task_ttl',
                    expireAfterSeconds: 36000
                }
            ]
        })raw_delimiter");
    auto inputOpMsg = monger::OpMsgRequest::fromDBAndBody("ttl_index_db", inputObj);
    auto output = performRpc(client, inputOpMsg);

    ASSERT(output.hasField("ok")) << output;
    ASSERT(output.getField("ok").numberDouble() != 1.0) << output;
}

TEST_F(MongerdbCAPITest, TrimMemory) {
    // create the client object
    auto client = createClient();

    // craft the isMaster message
    monger::BSONObj inputObj = monger::fromjson("{trimMemory: 'aggressive'}");
    auto inputOpMsg = monger::OpMsgRequest::fromDBAndBody("admin", inputObj);
    performRpc(client, inputOpMsg);
}

TEST_F(MongerdbCAPITest, BatteryLevel) {
    // create the client object
    auto client = createClient();

    // craft the isMaster message
    monger::BSONObj inputObj = monger::fromjson("{setBatteryLevel: 'low'}");
    auto inputOpMsg = monger::OpMsgRequest::fromDBAndBody("admin", inputObj);
    performRpc(client, inputOpMsg);
}


TEST_F(MongerdbCAPITest, InsertDocument) {
    auto client = createClient();

    monger::BSONObj insertObj = monger::fromjson(
        "{insert: 'collection_name', documents: [{firstName: 'Monger', lastName: 'DB', age: 10}]}");
    auto insertOpMsg = monger::OpMsgRequest::fromDBAndBody("db_name", insertObj);
    auto outputBSON = performRpc(client, insertOpMsg);
    ASSERT(outputBSON.hasField("n"));
    ASSERT(outputBSON.getIntField("n") == 1);
    ASSERT(outputBSON.hasField("ok"));
    ASSERT(outputBSON.getField("ok").numberDouble() == 1.0);
}

TEST_F(MongerdbCAPITest, InsertMultipleDocuments) {
    auto client = createClient();

    monger::BSONObj insertObj = monger::fromjson(
        "{insert: 'collection_name', documents: [{firstName: 'doc1FirstName', lastName: "
        "'doc1LastName', age: 30}, {firstName: 'doc2FirstName', lastName: 'doc2LastName', age: "
        "20}]}");
    auto insertOpMsg = monger::OpMsgRequest::fromDBAndBody("db_name", insertObj);
    auto outputBSON = performRpc(client, insertOpMsg);
    ASSERT(outputBSON.hasField("n"));
    ASSERT(outputBSON.getIntField("n") == 2);
    ASSERT(outputBSON.hasField("ok"));
    ASSERT(outputBSON.getField("ok").numberDouble() == 1.0);
}

TEST_F(MongerdbCAPITest, KillOp) {
    auto client = createClient();

    monger::stdx::thread killOpThread([this]() {
        auto client = createClient();

        monger::BSONObj currentOpObj = monger::fromjson("{currentOp: 1}");
        auto currentOpMsg = monger::OpMsgRequest::fromDBAndBody("admin", currentOpObj);
        monger::BSONObj outputBSON;

        // Wait for the sleep command to start in the main test thread.
        int opid = -1;
        do {
            outputBSON = performRpc(client, currentOpMsg);
            auto inprog = outputBSON.getObjectField("inprog");

            // See if we find the sleep command among the running commands
            for (const auto& elt : inprog) {
                auto inprogObj = inprog.getObjectField(elt.fieldNameStringData());
                std::string ns = inprogObj.getStringField("ns");
                if (ns == "admin.$cmd") {
                    opid = inprogObj.getIntField("opid");
                    break;
                }
            }
        } while (opid == -1);

        // Sleep command found, kill it.
        std::stringstream ss;
        ss << "{'killOp': 1, 'op': " << opid << "}";
        monger::BSONObj killOpObj = monger::fromjson(ss.str());
        auto killOpMsg = monger::OpMsgRequest::fromDBAndBody("admin", killOpObj);
        outputBSON = performRpc(client, killOpMsg);

        ASSERT(outputBSON.hasField("ok"));
        ASSERT(outputBSON.getField("ok").numberDouble() == 1.0);
    });

    monger::BSONObj sleepObj = monger::fromjson("{'sleep': {'secs': 1000}}");
    auto sleepOpMsg = monger::OpMsgRequest::fromDBAndBody("admin", sleepObj);
    auto outputBSON = performRpc(client, sleepOpMsg);

    ASSERT(outputBSON.hasField("ok"));
    ASSERT(outputBSON.getField("ok").numberDouble() != 1.0);
    ASSERT(outputBSON.getIntField("code") == monger::ErrorCodes::Interrupted);

    killOpThread.join();
}

TEST_F(MongerdbCAPITest, ReadDB) {
    auto client = createClient();

    monger::BSONObj findObj = monger::fromjson("{find: 'collection_name', limit: 2}");
    auto findMsg = monger::OpMsgRequest::fromDBAndBody("db_name", findObj);
    auto outputBSON = performRpc(client, findMsg);


    ASSERT(outputBSON.valid(monger::BSONVersion::kLatest));
    ASSERT(outputBSON.hasField("cursor"));
    ASSERT(outputBSON.getField("cursor").embeddedObject().hasField("firstBatch"));
    monger::BSONObj arrObj =
        outputBSON.getField("cursor").embeddedObject().getField("firstBatch").embeddedObject();
    ASSERT(arrObj.couldBeArray());

    monger::BSONObjIterator i(arrObj);
    int index = 0;
    while (i.moreWithEOO()) {
        monger::BSONElement e = i.next();
        if (e.eoo())
            break;
        index++;
    }
    ASSERT(index == 2);
}

TEST_F(MongerdbCAPITest, InsertAndRead) {
    auto client = createClient();

    monger::BSONObj insertObj = monger::fromjson(
        "{insert: 'collection_name', documents: [{firstName: 'Monger', lastName: 'DB', age: 10}]}");
    auto insertOpMsg = monger::OpMsgRequest::fromDBAndBody("db_name", insertObj);
    auto outputBSON1 = performRpc(client, insertOpMsg);
    ASSERT(outputBSON1.valid(monger::BSONVersion::kLatest));
    ASSERT(outputBSON1.hasField("n"));
    ASSERT(outputBSON1.getIntField("n") == 1);
    ASSERT(outputBSON1.hasField("ok"));
    ASSERT(outputBSON1.getField("ok").numberDouble() == 1.0);

    monger::BSONObj findObj = monger::fromjson("{find: 'collection_name', limit: 1}");
    auto findMsg = monger::OpMsgRequest::fromDBAndBody("db_name", findObj);
    auto outputBSON2 = performRpc(client, findMsg);
    ASSERT(outputBSON2.valid(monger::BSONVersion::kLatest));
    ASSERT(outputBSON2.hasField("cursor"));
    ASSERT(outputBSON2.getField("cursor").embeddedObject().hasField("firstBatch"));
    monger::BSONObj arrObj =
        outputBSON2.getField("cursor").embeddedObject().getField("firstBatch").embeddedObject();
    ASSERT(arrObj.couldBeArray());

    monger::BSONObjIterator i(arrObj);
    int index = 0;
    while (i.moreWithEOO()) {
        monger::BSONElement e = i.next();
        if (e.eoo())
            break;
        index++;
    }
    ASSERT(index == 1);
}

TEST_F(MongerdbCAPITest, InsertAndReadDifferentClients) {
    auto client1 = createClient();
    auto client2 = createClient();

    monger::BSONObj insertObj = monger::fromjson(
        "{insert: 'collection_name', documents: [{firstName: 'Monger', lastName: 'DB', age: 10}]}");
    auto insertOpMsg = monger::OpMsgRequest::fromDBAndBody("db_name", insertObj);
    auto outputBSON1 = performRpc(client1, insertOpMsg);
    ASSERT(outputBSON1.valid(monger::BSONVersion::kLatest));
    ASSERT(outputBSON1.hasField("n"));
    ASSERT(outputBSON1.getIntField("n") == 1);
    ASSERT(outputBSON1.hasField("ok"));
    ASSERT(outputBSON1.getField("ok").numberDouble() == 1.0);

    monger::BSONObj findObj = monger::fromjson("{find: 'collection_name', limit: 1}");
    auto findMsg = monger::OpMsgRequest::fromDBAndBody("db_name", findObj);
    auto outputBSON2 = performRpc(client2, findMsg);
    ASSERT(outputBSON2.valid(monger::BSONVersion::kLatest));
    ASSERT(outputBSON2.hasField("cursor"));
    ASSERT(outputBSON2.getField("cursor").embeddedObject().hasField("firstBatch"));
    monger::BSONObj arrObj =
        outputBSON2.getField("cursor").embeddedObject().getField("firstBatch").embeddedObject();
    ASSERT(arrObj.couldBeArray());

    monger::BSONObjIterator i(arrObj);
    int index = 0;
    while (i.moreWithEOO()) {
        monger::BSONElement e = i.next();
        if (e.eoo())
            break;
        index++;
    }
    ASSERT(index == 1);
}

TEST_F(MongerdbCAPITest, InsertAndDelete) {
    auto client = createClient();
    monger::BSONObj insertObj = monger::fromjson(
        "{insert: 'collection_name', documents: [{firstName: 'toDelete', lastName: 'notImportant', "
        "age: 10}]}");
    auto insertOpMsg = monger::OpMsgRequest::fromDBAndBody("db_name", insertObj);
    auto outputBSON1 = performRpc(client, insertOpMsg);
    ASSERT(outputBSON1.valid(monger::BSONVersion::kLatest));
    ASSERT(outputBSON1.hasField("n"));
    ASSERT(outputBSON1.getIntField("n") == 1);
    ASSERT(outputBSON1.hasField("ok"));
    ASSERT(outputBSON1.getField("ok").numberDouble() == 1.0);


    // Delete
    monger::BSONObj deleteObj = monger::fromjson(
        "{delete: 'collection_name', deletes:   [{q: {firstName: 'toDelete', age: 10}, limit: "
        "1}]}");
    auto deleteOpMsg = monger::OpMsgRequest::fromDBAndBody("db_name", deleteObj);
    auto outputBSON2 = performRpc(client, deleteOpMsg);
    ASSERT(outputBSON2.valid(monger::BSONVersion::kLatest));
    ASSERT(outputBSON2.hasField("n"));
    ASSERT(outputBSON2.getIntField("n") == 1);
    ASSERT(outputBSON2.hasField("ok"));
    ASSERT(outputBSON2.getField("ok").numberDouble() == 1.0);
}


TEST_F(MongerdbCAPITest, InsertAndUpdate) {
    auto client = createClient();

    monger::BSONObj insertObj = monger::fromjson(
        "{insert: 'collection_name', documents: [{firstName: 'toUpdate', lastName: 'notImportant', "
        "age: 10}]}");
    auto insertOpMsg = monger::OpMsgRequest::fromDBAndBody("db_name", insertObj);
    auto outputBSON1 = performRpc(client, insertOpMsg);
    ASSERT(outputBSON1.valid(monger::BSONVersion::kLatest));
    ASSERT(outputBSON1.hasField("n"));
    ASSERT(outputBSON1.getIntField("n") == 1);
    ASSERT(outputBSON1.hasField("ok"));
    ASSERT(outputBSON1.getField("ok").numberDouble() == 1.0);


    // Update
    monger::BSONObj updateObj = monger::fromjson(
        "{update: 'collection_name', updates: [ {q: {firstName: 'toUpdate', age: 10}, u: {'$inc': "
        "{age: 5}}}]}");
    auto updateOpMsg = monger::OpMsgRequest::fromDBAndBody("db_name", updateObj);
    auto outputBSON2 = performRpc(client, updateOpMsg);
    ASSERT(outputBSON2.valid(monger::BSONVersion::kLatest));
    ASSERT(outputBSON2.hasField("ok"));
    ASSERT(outputBSON2.getField("ok").numberDouble() == 1.0);
    ASSERT(outputBSON2.hasField("nModified"));
    ASSERT(outputBSON2.getIntField("nModified") == 1);
}

TEST_F(MongerdbCAPITest, RunListCommands) {
    auto client = createClient();

    std::vector<std::string> whitelist = {
        "_hashBSONElement",
        "aggregate",
        "buildInfo",
        "collMod",
        "collStats",
        "configureFailPoint",
        "count",
        "create",
        "createIndexes",
        "currentOp",
        "dataSize",
        "dbStats",
        "delete",
        "distinct",
        "drop",
        "dropDatabase",
        "dropIndexes",
        "echo",
        "endSessions",
        "explain",
        "find",
        "findAndModify",
        "getLastError",
        "getMore",
        "getParameter",
        "httpClientRequest",
        "insert",
        "isMaster",
        "killCursors",
        "killOp",
        "killSessions",
        "killAllSessions",
        "killAllSessionsByPattern",
        "listCollections",
        "listCommands",
        "listDatabases",
        "listIndexes",
        "lockInfo",
        "ping",
        "planCacheClear",
        "planCacheClearFilters",
        "planCacheListFilters",
        "planCacheListPlans",
        "planCacheListQueryShapes",
        "planCacheSetFilter",
        "reIndex",
        "refreshLogicalSessionCacheNow",
        "refreshSessions",
        "renameCollection",
        "repairCursor",
        "repairDatabase",
        "resetError",
        "serverStatus",
        "setBatteryLevel",
        "setParameter",
        "sleep",
        "startSession",
        "trimMemory",
        "twoPhaseCreateIndexes",
        "update",
        "validate",
    };
    std::sort(whitelist.begin(), whitelist.end());

    monger::BSONObj listCommandsObj = monger::fromjson("{ listCommands: 1 }");
    auto listCommandsOpMsg = monger::OpMsgRequest::fromDBAndBody("db_name", listCommandsObj);
    auto output = performRpc(client, listCommandsOpMsg);
    auto commandsBSON = output["commands"];
    std::vector<std::string> commands;
    for (const auto& element : commandsBSON.Obj()) {
        commands.push_back(element.fieldNameStringData().toString());
    }
    std::sort(commands.begin(), commands.end());

    std::vector<std::string> missing;
    std::vector<std::string> unsupported;
    std::set_difference(whitelist.begin(),
                        whitelist.end(),
                        commands.begin(),
                        commands.end(),
                        std::back_inserter(missing));
    std::set_difference(commands.begin(),
                        commands.end(),
                        whitelist.begin(),
                        whitelist.end(),
                        std::back_inserter(unsupported));

    if (!missing.empty()) {
        std::cout << "\nMissing commands from the embedded binary:\n";
    }
    for (auto&& cmd : missing) {
        std::cout << cmd << "\n";
    }
    if (!unsupported.empty()) {
        std::cout << "\nUnsupported commands in the embedded binary:\n";
    }
    for (auto&& cmd : unsupported) {
        std::cout << cmd << "\n";
    }

    ASSERT(missing.empty());
    ASSERT(unsupported.empty());
}

// This test is temporary to make sure that only one database can be created
// This restriction may be relaxed at a later time
TEST_F(MongerdbCAPITest, CreateMultipleDBs) {
    auto status = makeStatusPtr();
    ASSERT(status.get());
    monger_embedded_v1_instance* db2 = monger_embedded_v1_instance_create(lib, nullptr, status.get());
    ASSERT(db2 == nullptr);
    ASSERT_EQUALS(monger_embedded_v1_status_get_error(status.get()),
                  MONGO_EMBEDDED_V1_ERROR_DB_MAX_OPEN);
}
}  // namespace

// Define main function as an entry to these tests.
// These test functions cannot use the main() defined for unittests because they
// call runGlobalInitializers(). The embedded C API calls mongerDbMain() which
// calls runGlobalInitializers().
int main(const int argc, const char* const* const argv) {
    moe::Environment environment;
    moe::OptionSection options;

    auto ret = monger::embedded::addMongerEmbeddedTestOptions(&options);
    if (!ret.isOK()) {
        std::cerr << ret << std::endl;
        return EXIT_FAILURE;
    }

    std::map<std::string, std::string> env;
    ret = moe::OptionsParser().run(
        options, std::vector<std::string>(argv, argv + argc), env, &environment);
    if (!ret.isOK()) {
        std::cerr << options.helpString();
        return EXIT_FAILURE;
    }
    if (environment.count("tempPath")) {
        ::monger::unittest::TempDir::setTempPath(environment["tempPath"].as<std::string>());
    }

    ::monger::clearSignalMask();
    ::monger::setupSynchronousSignalHandlers();
    ::monger::serverGlobalParams.noUnixSocket = true;

    // Allocate an error descriptor for use in non-configured tests
    const auto status = makeStatusPtr();

    monger::setTestCommandsEnabled(true);

    // Perform one cycle of global initialization/deinitialization before running test. This will
    // make sure everything that is needed is setup for the unittest infrastructure.
    // The reason this works is that the unittest system relies on other systems being initialized
    // through global init and deinitialize just deinitializes systems that explicitly supports
    // deinit leaving the systems unittest needs initialized.
    const char* null_argv[1] = {nullptr};
    ret = monger::runGlobalInitializers(0, null_argv, nullptr);
    if (!ret.isOK()) {
        std::cerr << "Global initilization failed";
        return EXIT_FAILURE;
    }

    ret = monger::runGlobalDeinitializers();
    if (!ret.isOK()) {
        std::cerr << "Global deinitilization failed";
        return EXIT_FAILURE;
    }

    // Check so we can initialize the library without providing init params
    monger_embedded_v1_lib* lib = monger_embedded_v1_lib_init(nullptr, status.get());
    if (lib == nullptr) {
        std::cerr << "monger_embedded_v1_init() failed with "
                  << monger_embedded_v1_status_get_error(status.get()) << ": "
                  << monger_embedded_v1_status_get_explanation(status.get()) << std::endl;
        return EXIT_FAILURE;
    }

    if (monger_embedded_v1_lib_fini(lib, status.get()) != MONGO_EMBEDDED_V1_SUCCESS) {
        std::cerr << "monger_embedded_v1_fini() failed with "
                  << monger_embedded_v1_status_get_error(status.get()) << ": "
                  << monger_embedded_v1_status_get_explanation(status.get()) << std::endl;
        return EXIT_FAILURE;
    }

    // Initialize the library with a log callback and test so we receive at least one callback
    // during the lifetime of the test
    monger_embedded_v1_init_params params{};

    bool receivedCallback = false;
    params.log_flags = MONGO_EMBEDDED_V1_LOG_STDOUT | MONGO_EMBEDDED_V1_LOG_CALLBACK;
    params.log_callback = [](void* user_data,
                             const char* message,
                             const char* component,
                             const char* context,
                             int severety) {
        ASSERT(message);
        ASSERT(component);
        *reinterpret_cast<bool*>(user_data) = true;
    };
    params.log_user_data = &receivedCallback;

    lib = monger_embedded_v1_lib_init(&params, nullptr);
    if (lib == nullptr) {
        std::cerr << "monger_embedded_v1_init() failed with "
                  << monger_embedded_v1_status_get_error(status.get()) << ": "
                  << monger_embedded_v1_status_get_explanation(status.get()) << std::endl;
    }

    // Attempt to create an embedded instance to make sure something gets logged. This will probably
    // fail but that's fine.
    monger_embedded_v1_instance* instance = monger_embedded_v1_instance_create(lib, nullptr, nullptr);
    if (instance) {
        monger_embedded_v1_instance_destroy(instance, nullptr);
    }

    if (monger_embedded_v1_lib_fini(lib, nullptr) != MONGO_EMBEDDED_V1_SUCCESS) {
        std::cerr << "monger_embedded_v1_fini() failed with "
                  << monger_embedded_v1_status_get_error(status.get()) << ": "
                  << monger_embedded_v1_status_get_explanation(status.get()) << std::endl;
    }

    if (!receivedCallback) {
        std::cerr << "Did not get a log callback." << std::endl;
        return EXIT_FAILURE;
    }

    const auto result = ::monger::unittest::Suite::run(std::vector<std::string>(), "", 1);

    globalTempDir.reset();
    monger::quickExit(result);
}
