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

#include <set>
#include <string>
#include <vector>

#include "monger/client/read_preference.h"
#include "monger/client/remote_command_targeter_factory_mock.h"
#include "monger/client/remote_command_targeter_mock.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/s/config/sharding_catalog_manager.h"
#include "monger/executor/network_interface_mock.h"
#include "monger/executor/task_executor.h"
#include "monger/s/catalog/type_database.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/config_server_test_fixture.h"
#include "monger/s/grid.h"
#include "monger/s/shard_key_pattern.h"
#include "monger/stdx/future.h"
#include "monger/util/log.h"
#include "monger/util/scopeguard.h"

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

class CreateCollectionTest : public ConfigServerTestFixture {
public:
    void expectCreate(const HostAndPort& receivingHost,
                      const NamespaceString& expectedNs,
                      Status response) {
        onCommand([&](const RemoteCommandRequest& request) {
            ASSERT_EQUALS(receivingHost, request.target);
            string cmdName = request.cmdObj.firstElement().fieldName();

            ASSERT_EQUALS("create", cmdName);

            const NamespaceString nss(request.dbname, request.cmdObj.firstElement().String());
            ASSERT_EQUALS(expectedNs.toString(), nss.toString());

            BSONObjBuilder responseBuilder;
            CommandHelpers::appendCommandStatusNoThrow(responseBuilder, response);
            return responseBuilder.obj();
        });
    }

    void expectListCollection(const HostAndPort& receivingHost,
                              const string& expectedDb,
                              StatusWith<BSONObj> collectionOptionsReponse,
                              const UUID& uuid) {
        onCommand([&](const RemoteCommandRequest& request) {
            ASSERT_EQUALS(receivingHost, request.target);
            string cmdName = request.cmdObj.firstElement().fieldName();

            ASSERT_EQUALS("listCollections", cmdName);

            ASSERT_EQUALS(expectedDb, request.dbname);

            BSONObjBuilder responseBuilder;

            if (!collectionOptionsReponse.isOK()) {
                CommandHelpers::appendCommandStatusNoThrow(responseBuilder,
                                                           collectionOptionsReponse.getStatus());
            } else {
                BSONObjBuilder listCollResponse(responseBuilder.subobjStart("cursor"));
                BSONArrayBuilder collArrayBuilder(listCollResponse.subarrayStart("firstBatch"));


                BSONObjBuilder collBuilder;
                collBuilder.append("options", collectionOptionsReponse.getValue());
                collBuilder.append("info", BSON("uuid" << uuid));
                collArrayBuilder.append(collBuilder.obj());

                collArrayBuilder.done();
                listCollResponse.done();
            }

            return responseBuilder.obj();
        });
    }

protected:
    void setUp() override {
        ConfigServerTestFixture::setUp();

        extraShard.setName("extra");
        extraShard.setHost("a:10");

        testPrimaryShard.setName("primary");
        testPrimaryShard.setHost("b:20");

        setupShards({extraShard, testPrimaryShard});

        // Prime the shard registry with information about the existing shards
        shardRegistry()->reload(operationContext());

        // Set up all the target mocks return values.
        RemoteCommandTargeterMock::get(
            uassertStatusOK(shardRegistry()->getShard(operationContext(), extraShard.getName()))
                ->getTargeter())
            ->setFindHostReturnValue(HostAndPort(extraShard.getHost()));
        RemoteCommandTargeterMock::get(
            uassertStatusOK(
                shardRegistry()->getShard(operationContext(), testPrimaryShard.getName()))
                ->getTargeter())
            ->setFindHostReturnValue(HostAndPort(testPrimaryShard.getHost()));
    }

    const ShardType& getPrimaryShard() const {
        return testPrimaryShard;
    }

private:
    ShardType extraShard;
    ShardType testPrimaryShard;
};

TEST_F(CreateCollectionTest, BaseCase) {
    NamespaceString testNS("test", "foo");
    const auto& primaryShard = getPrimaryShard();

    setupDatabase(testNS.db().toString(), {primaryShard.getName()}, false);

    CollectionOptions requestOptions;
    requestOptions.capped = true;
    requestOptions.cappedSize = 256;

    auto future = launchAsync([this, &testNS, &requestOptions] {
        ThreadClient tc("BaseCaseTest", getGlobalServiceContext());
        auto opCtx = cc().makeOperationContext();
        ShardingCatalogManager::get(opCtx.get())
            ->createCollection(opCtx.get(), testNS, requestOptions);
    });

    HostAndPort primaryHost(primaryShard.getHost());
    expectCreate(primaryHost, testNS, Status::OK());

    auto uuid = UUID::gen();
    auto options = fromjson("{ capped: true, size: 256 }");
    expectListCollection(primaryHost, testNS.db().toString(), options, uuid);
    future.default_timed_get();
}

}  // namespace
}  // namespace monger
