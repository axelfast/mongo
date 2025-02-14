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

#include "monger/platform/basic.h"

#include "monger/bson/bsonobjbuilder.h"
#include "monger/client/remote_command_targeter_mock.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/dbdirectclient.h"
#include "monger/db/namespace_string.h"
#include "monger/db/s/implicit_create_collection.h"
#include "monger/s/request_types/create_collection_gen.h"
#include "monger/s/shard_server_test_fixture.h"
#include "monger/unittest/unittest.h"
#include "monger/util/assert_util.h"

namespace monger {
namespace {

class ImplicitCreateTest : public ShardServerTestFixture {
public:
    void expectConfigCreate(const NamespaceString& expectedNss, const Status& response) {
        onCommand([&](const executor::RemoteCommandRequest& request) {
            auto configHostStatus = configTargeterMock()->findHost(nullptr, {});
            ASSERT_OK(configHostStatus.getStatus());
            auto configHost = configHostStatus.getValue();

            ASSERT_EQ(configHost, request.target);
            auto cmdName = request.cmdObj.firstElement().fieldName();
            ASSERT_EQ(ConfigsvrCreateCollection::kCommandName, cmdName);

            ASSERT_EQ("admin", request.dbname);
            ASSERT_EQ(expectedNss.ns(), request.cmdObj.firstElement().String());

            BSONObjBuilder responseBuilder;
            CommandHelpers::appendCommandStatusNoThrow(responseBuilder, response);
            return responseBuilder.obj();
        });
    }
};

TEST_F(ImplicitCreateTest, NormalCreate) {
    const NamespaceString kNs("test.user");
    auto future = launchAsync([this, &kNs] {
        ThreadClient tc("Test", getGlobalServiceContext());
        auto opCtx = cc().makeOperationContext();
        ASSERT_OK(onCannotImplicitlyCreateCollection(opCtx.get(), kNs));
    });

    expectConfigCreate(kNs, Status::OK());

    future.default_timed_get();
}

TEST_F(ImplicitCreateTest, CanCallOnCannotImplicitAgainAfterError) {
    const NamespaceString kNs("test.user");
    auto future = launchAsync([this, &kNs] {
        ThreadClient tc("Test", getGlobalServiceContext());
        auto opCtx = cc().makeOperationContext();
        auto status = onCannotImplicitlyCreateCollection(opCtx.get(), kNs);
        ASSERT_EQ(ErrorCodes::FailPointEnabled, status);
    });

    // return a non retryable error (just for testing) so the handler won't retry.
    expectConfigCreate(kNs, {ErrorCodes::FailPointEnabled, "deliberate error"});

    future.default_timed_get();


    // Retry, but this time config server will return success

    future = launchAsync([this, &kNs] {
        ThreadClient tc("Test", getGlobalServiceContext());
        auto opCtx = cc().makeOperationContext();
        ASSERT_OK(onCannotImplicitlyCreateCollection(opCtx.get(), kNs));
    });

    expectConfigCreate(kNs, Status::OK());

    future.default_timed_get();
}

TEST_F(ImplicitCreateTest, ShouldNotCallConfigCreateIfCollectionExists) {
    const NamespaceString kNs("test.user");
    auto future = launchAsync([this, &kNs] {
        ThreadClient tc("Test", getGlobalServiceContext());
        auto opCtx = cc().makeOperationContext();
        auto status = onCannotImplicitlyCreateCollection(opCtx.get(), kNs);
        ASSERT_EQ(ErrorCodes::FailPointEnabled, status);
    });

    // return a non retryable error (just for testing) so the handler won't retry.
    expectConfigCreate(kNs, {ErrorCodes::FailPointEnabled, "deliberate error"});

    future.default_timed_get();

    // Simulate config server successfully creating the collection despite returning error.
    DBDirectClient client(operationContext());
    BSONObj result;
    ASSERT_TRUE(
        client.runCommand(kNs.db().toString(), BSON("create" << kNs.coll().toString()), result));

    // Retry, but this time config server will return success

    future = launchAsync([this, &kNs] {
        ThreadClient tc("Test", getGlobalServiceContext());
        auto opCtx = cc().makeOperationContext();
        ASSERT_OK(onCannotImplicitlyCreateCollection(opCtx.get(), kNs));
    });

    // Not expecting this shard to send any remote command.

    future.default_timed_get();
}

}  // unnamed namespace
}  // namespace monger
