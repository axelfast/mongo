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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kSharding

#include "monger/platform/basic.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "monger/client/remote_command_targeter_mock.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/ops/write_ops.h"
#include "monger/db/storage/duplicate_key_error_info.h"
#include "monger/db/write_concern.h"
#include "monger/executor/network_interface_mock.h"
#include "monger/executor/task_executor.h"
#include "monger/rpc/metadata/repl_set_metadata.h"
#include "monger/s/catalog/dist_lock_manager_mock.h"
#include "monger/s/catalog/sharding_catalog_client_impl.h"
#include "monger/s/catalog/type_changelog.h"
#include "monger/s/catalog/type_chunk.h"
#include "monger/s/catalog/type_collection.h"
#include "monger/s/catalog/type_database.h"
#include "monger/s/catalog/type_shard.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/grid.h"
#include "monger/s/sharding_router_test_fixture.h"
#include "monger/s/write_ops/batched_command_response.h"
#include "monger/stdx/future.h"
#include "monger/util/log.h"

namespace monger {
namespace {

using executor::NetworkInterfaceMock;
using executor::RemoteCommandRequest;
using executor::RemoteCommandResponse;
using executor::TaskExecutor;
using std::set;
using std::string;
using std::vector;
using unittest::assertGet;

using InsertRetryTest = ShardingTestFixture;
using UpdateRetryTest = ShardingTestFixture;

const NamespaceString kTestNamespace("config.TestColl");
const HostAndPort kTestHosts[] = {
    HostAndPort("TestHost1:12345"), HostAndPort("TestHost2:12345"), HostAndPort("TestHost3:12345")};

Status getMockDuplicateKeyError() {
    return {DuplicateKeyErrorInfo(BSON("mock" << 1), BSON("" << 1)), "Mock duplicate key error"};
}

TEST_F(InsertRetryTest, RetryOnInterruptedAndNetworkErrorSuccess) {
    configTargeter()->setFindHostReturnValue({kTestHosts[0]});

    BSONObj objToInsert = BSON("_id" << 1 << "Value"
                                     << "TestValue");

    auto future = launchAsync([&] {
        Status status =
            catalogClient()->insertConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToInsert,
                                                  ShardingCatalogClient::kMajorityWriteConcern);
        ASSERT_OK(status);
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[0]);
        configTargeter()->setFindHostReturnValue({kTestHosts[1]});
        return Status(ErrorCodes::InterruptedDueToReplStateChange, "Interruption");
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[1]);
        configTargeter()->setFindHostReturnValue({kTestHosts[2]});
        return Status(ErrorCodes::NetworkTimeout, "Network timeout");
    });

    expectInserts(kTestNamespace, {objToInsert});

    future.default_timed_get();
}

TEST_F(InsertRetryTest, RetryOnNetworkErrorFails) {
    configTargeter()->setFindHostReturnValue({kTestHosts[0]});

    BSONObj objToInsert = BSON("_id" << 1 << "Value"
                                     << "TestValue");

    auto future = launchAsync([&] {
        Status status =
            catalogClient()->insertConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToInsert,
                                                  ShardingCatalogClient::kMajorityWriteConcern);
        ASSERT_EQ(ErrorCodes::NetworkTimeout, status.code());
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[0]);
        configTargeter()->setFindHostReturnValue({kTestHosts[1]});
        return Status(ErrorCodes::NetworkTimeout, "Network timeout");
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[1]);
        configTargeter()->setFindHostReturnValue({kTestHosts[2]});
        return Status(ErrorCodes::NetworkTimeout, "Network timeout");
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[2]);
        return Status(ErrorCodes::NetworkTimeout, "Network timeout");
    });

    future.default_timed_get();
}

TEST_F(InsertRetryTest, DuplicateKeyErrorAfterNetworkErrorMatch) {
    configTargeter()->setFindHostReturnValue({kTestHosts[0]});

    BSONObj objToInsert = BSON("_id" << 1 << "Value"
                                     << "TestValue");

    auto future = launchAsync([&] {
        Status status =
            catalogClient()->insertConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToInsert,
                                                  ShardingCatalogClient::kMajorityWriteConcern);
        ASSERT_OK(status);
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[0]);
        configTargeter()->setFindHostReturnValue({kTestHosts[1]});
        return Status(ErrorCodes::NetworkTimeout, "Network timeout");
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[1]);
        return getMockDuplicateKeyError();
    });

    onFindCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[1]);
        auto query =
            assertGet(QueryRequest::makeFromFindCommand(kTestNamespace, request.cmdObj, false));
        ASSERT_BSONOBJ_EQ(BSON("_id" << 1), query->getFilter());

        return vector<BSONObj>{objToInsert};
    });

    future.default_timed_get();
}

TEST_F(InsertRetryTest, DuplicateKeyErrorAfterNetworkErrorNotFound) {
    configTargeter()->setFindHostReturnValue({kTestHosts[0]});

    BSONObj objToInsert = BSON("_id" << 1 << "Value"
                                     << "TestValue");

    auto future = launchAsync([&] {
        Status status =
            catalogClient()->insertConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToInsert,
                                                  ShardingCatalogClient::kMajorityWriteConcern);
        ASSERT_EQ(ErrorCodes::DuplicateKey, status.code());
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[0]);
        configTargeter()->setFindHostReturnValue({kTestHosts[1]});
        return Status(ErrorCodes::NetworkTimeout, "Network timeout");
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[1]);
        return getMockDuplicateKeyError();
    });

    onFindCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[1]);
        auto query =
            assertGet(QueryRequest::makeFromFindCommand(kTestNamespace, request.cmdObj, false));
        ASSERT_BSONOBJ_EQ(BSON("_id" << 1), query->getFilter());

        return vector<BSONObj>();
    });

    future.default_timed_get();
}

TEST_F(InsertRetryTest, DuplicateKeyErrorAfterNetworkErrorMismatch) {
    configTargeter()->setFindHostReturnValue({kTestHosts[0]});

    BSONObj objToInsert = BSON("_id" << 1 << "Value"
                                     << "TestValue");

    auto future = launchAsync([&] {
        Status status =
            catalogClient()->insertConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToInsert,
                                                  ShardingCatalogClient::kMajorityWriteConcern);
        ASSERT_EQ(ErrorCodes::DuplicateKey, status.code());
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[0]);
        configTargeter()->setFindHostReturnValue({kTestHosts[1]});
        return Status(ErrorCodes::NetworkTimeout, "Network timeout");
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[1]);
        return getMockDuplicateKeyError();
    });

    onFindCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[1]);
        auto query =
            assertGet(QueryRequest::makeFromFindCommand(kTestNamespace, request.cmdObj, false));
        ASSERT_BSONOBJ_EQ(BSON("_id" << 1), query->getFilter());

        return vector<BSONObj>{BSON("_id" << 1 << "Value"
                                          << "TestValue has changed")};
    });

    future.default_timed_get();
}

TEST_F(InsertRetryTest, DuplicateKeyErrorAfterWriteConcernFailureMatch) {
    configTargeter()->setFindHostReturnValue({kTestHosts[0]});

    BSONObj objToInsert = BSON("_id" << 1 << "Value"
                                     << "TestValue");

    auto future = launchAsync([&] {
        Status status =
            catalogClient()->insertConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToInsert,
                                                  ShardingCatalogClient::kMajorityWriteConcern);
        ASSERT_OK(status);
    });

    onCommand([&](const RemoteCommandRequest& request) {
        const auto opMsgRequest = OpMsgRequest::fromDBAndBody(request.dbname, request.cmdObj);
        const auto insertOp = InsertOp::parse(opMsgRequest);
        ASSERT_EQUALS(kTestNamespace, insertOp.getNamespace());

        BatchedCommandResponse response;
        response.setStatus(Status::OK());
        response.setN(1);

        auto wcError = std::make_unique<WriteConcernErrorDetail>();

        WriteConcernResult wcRes;
        wcRes.err = "timeout";
        wcRes.wTimedOut = true;

        wcError->setStatus({ErrorCodes::NetworkTimeout, "Failed to wait for write concern"});
        wcError->setErrInfo(BSON("wtimeout" << true));

        response.setWriteConcernError(wcError.release());

        return response.toBSON();
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[0]);
        return getMockDuplicateKeyError();
    });

    onFindCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQ(request.target, kTestHosts[0]);
        auto query =
            assertGet(QueryRequest::makeFromFindCommand(kTestNamespace, request.cmdObj, false));
        ASSERT_BSONOBJ_EQ(BSON("_id" << 1), query->getFilter());

        return vector<BSONObj>{objToInsert};
    });

    future.default_timed_get();
}

TEST_F(UpdateRetryTest, Success) {
    configTargeter()->setFindHostReturnValue(HostAndPort("TestHost1"));

    BSONObj objToUpdate = BSON("_id" << 1 << "Value"
                                     << "TestValue");
    BSONObj updateExpr = BSON("$set" << BSON("Value"
                                             << "NewTestValue"));

    auto future = launchAsync([&] {
        auto status =
            catalogClient()->updateConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToUpdate,
                                                  updateExpr,
                                                  false,
                                                  ShardingCatalogClient::kMajorityWriteConcern);
        ASSERT_OK(status);
    });

    onCommand([&](const RemoteCommandRequest& request) {
        const auto opMsgRequest = OpMsgRequest::fromDBAndBody(request.dbname, request.cmdObj);
        const auto updateOp = UpdateOp::parse(opMsgRequest);
        ASSERT_EQUALS(kTestNamespace, updateOp.getNamespace());

        BatchedCommandResponse response;
        response.setStatus(Status::OK());
        response.setNModified(1);

        return response.toBSON();
    });

    future.default_timed_get();
}

TEST_F(UpdateRetryTest, NotMasterErrorReturnedPersistently) {
    configTargeter()->setFindHostReturnValue(HostAndPort("TestHost1"));

    BSONObj objToUpdate = BSON("_id" << 1 << "Value"
                                     << "TestValue");
    BSONObj updateExpr = BSON("$set" << BSON("Value"
                                             << "NewTestValue"));

    auto future = launchAsync([&] {
        auto status =
            catalogClient()->updateConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToUpdate,
                                                  updateExpr,
                                                  false,
                                                  ShardingCatalogClient::kMajorityWriteConcern);
        ASSERT_EQUALS(ErrorCodes::NotMaster, status);
    });

    for (int i = 0; i < 3; ++i) {
        onCommand([](const RemoteCommandRequest& request) {
            BSONObjBuilder bb;
            CommandHelpers::appendCommandStatusNoThrow(bb, {ErrorCodes::NotMaster, "not master"});
            return bb.obj();
        });
    }

    future.default_timed_get();
}

TEST_F(UpdateRetryTest, NotMasterReturnedFromTargeter) {
    configTargeter()->setFindHostReturnValue(Status(ErrorCodes::NotMaster, "not master"));

    BSONObj objToUpdate = BSON("_id" << 1 << "Value"
                                     << "TestValue");
    BSONObj updateExpr = BSON("$set" << BSON("Value"
                                             << "NewTestValue"));

    auto future = launchAsync([&] {
        auto status =
            catalogClient()->updateConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToUpdate,
                                                  updateExpr,
                                                  false,
                                                  ShardingCatalogClient::kMajorityWriteConcern);
        ASSERT_EQUALS(ErrorCodes::NotMaster, status);
    });

    future.default_timed_get();
}

TEST_F(UpdateRetryTest, NotMasterOnceSuccessAfterRetry) {
    HostAndPort host1("TestHost1");
    HostAndPort host2("TestHost2");
    configTargeter()->setFindHostReturnValue(host1);

    CollectionType collection;
    collection.setNs(NamespaceString("db.coll"));
    collection.setUpdatedAt(network()->now());
    collection.setUnique(true);
    collection.setEpoch(OID::gen());
    collection.setKeyPattern(KeyPattern(BSON("_id" << 1)));

    BSONObj objToUpdate = BSON("_id" << 1 << "Value"
                                     << "TestValue");
    BSONObj updateExpr = BSON("$set" << BSON("Value"
                                             << "NewTestValue"));

    auto future = launchAsync([&] {
        ASSERT_OK(
            catalogClient()->updateConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToUpdate,
                                                  updateExpr,
                                                  false,
                                                  ShardingCatalogClient::kMajorityWriteConcern));
    });

    onCommand([&](const RemoteCommandRequest& request) {
        ASSERT_EQUALS(host1, request.target);

        // Ensure that when the catalog manager tries to retarget after getting the
        // NotMaster response, it will get back a new target.
        configTargeter()->setFindHostReturnValue(host2);

        BSONObjBuilder bb;
        CommandHelpers::appendCommandStatusNoThrow(bb, {ErrorCodes::NotMaster, "not master"});
        return bb.obj();
    });

    onCommand([&](const RemoteCommandRequest& request) {
        const auto opMsgRequest = OpMsgRequest::fromDBAndBody(request.dbname, request.cmdObj);
        const auto updateOp = UpdateOp::parse(opMsgRequest);
        ASSERT_EQUALS(kTestNamespace, updateOp.getNamespace());

        BatchedCommandResponse response;
        response.setStatus(Status::OK());
        response.setNModified(1);

        return response.toBSON();
    });

    future.default_timed_get();
}

TEST_F(UpdateRetryTest, OperationInterruptedDueToPrimaryStepDown) {
    configTargeter()->setFindHostReturnValue({kTestHosts[0]});

    BSONObj objToUpdate = BSON("_id" << 1 << "Value"
                                     << "TestValue");
    BSONObj updateExpr = BSON("$set" << BSON("Value"
                                             << "NewTestValue"));

    auto future = launchAsync([&] {
        auto status =
            catalogClient()->updateConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToUpdate,
                                                  updateExpr,
                                                  false,
                                                  ShardingCatalogClient::kMajorityWriteConcern);
        ASSERT_OK(status);
    });

    onCommand([&](const RemoteCommandRequest& request) {
        const auto opMsgRequest = OpMsgRequest::fromDBAndBody(request.dbname, request.cmdObj);
        const auto updateOp = UpdateOp::parse(opMsgRequest);
        ASSERT_EQUALS(kTestNamespace, updateOp.getNamespace());

        BatchedCommandResponse response;
        response.setStatus(Status::OK());

        auto writeErrDetail = std::make_unique<WriteErrorDetail>();
        writeErrDetail->setIndex(0);
        writeErrDetail->setStatus(
            {ErrorCodes::InterruptedDueToReplStateChange, "Operation interrupted"});
        response.addToErrDetails(writeErrDetail.release());

        return response.toBSON();
    });

    onCommand([&](const RemoteCommandRequest& request) {
        const auto opMsgRequest = OpMsgRequest::fromDBAndBody(request.dbname, request.cmdObj);
        const auto updateOp = UpdateOp::parse(opMsgRequest);
        ASSERT_EQUALS(kTestNamespace, updateOp.getNamespace());

        BatchedCommandResponse response;
        response.setStatus(Status::OK());
        response.setNModified(1);

        return response.toBSON();
    });

    future.default_timed_get();
}

TEST_F(UpdateRetryTest, WriteConcernFailure) {
    configTargeter()->setFindHostReturnValue({kTestHosts[0]});

    BSONObj objToUpdate = BSON("_id" << 1 << "Value"
                                     << "TestValue");
    BSONObj updateExpr = BSON("$set" << BSON("Value"
                                             << "NewTestValue"));

    auto future = launchAsync([&] {
        auto status =
            catalogClient()->updateConfigDocument(operationContext(),
                                                  kTestNamespace,
                                                  objToUpdate,
                                                  updateExpr,
                                                  false,
                                                  ShardingCatalogClient::kMajorityWriteConcern);
        ASSERT_OK(status);
    });

    onCommand([&](const RemoteCommandRequest& request) {
        const auto opMsgRequest = OpMsgRequest::fromDBAndBody(request.dbname, request.cmdObj);
        const auto updateOp = UpdateOp::parse(opMsgRequest);
        ASSERT_EQUALS(kTestNamespace, updateOp.getNamespace());

        BatchedCommandResponse response;
        response.setStatus(Status::OK());
        response.setNModified(1);

        auto wcError = std::make_unique<WriteConcernErrorDetail>();

        WriteConcernResult wcRes;
        wcRes.err = "timeout";
        wcRes.wTimedOut = true;

        wcError->setStatus({ErrorCodes::NetworkTimeout, "Failed to wait for write concern"});
        wcError->setErrInfo(BSON("wtimeout" << true));

        response.setWriteConcernError(wcError.release());

        return response.toBSON();
    });

    onCommand([&](const RemoteCommandRequest& request) {
        const auto opMsgRequest = OpMsgRequest::fromDBAndBody(request.dbname, request.cmdObj);
        const auto updateOp = UpdateOp::parse(opMsgRequest);
        ASSERT_EQUALS(kTestNamespace, updateOp.getNamespace());

        BatchedCommandResponse response;
        response.setStatus(Status::OK());
        response.setNModified(0);

        return response.toBSON();
    });

    future.default_timed_get();
}

}  // namespace
}  // namespace monger
