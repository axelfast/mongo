/**
 *    Copyright (C) 2018-present MongerDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongerDB, Inc.
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

#include "monger/db/s/transaction_coordinator_test_fixture.h"

#include "monger/client/remote_command_targeter_mock.h"
#include "monger/db/commands/txn_cmds_gen.h"
#include "monger/db/commands/txn_two_phase_commit_cmds_gen.h"
#include "monger/db/operation_context.h"
#include "monger/s/catalog/sharding_catalog_client_mock.h"
#include "monger/s/catalog/type_shard.h"
#include "monger/unittest/unittest.h"
#include "monger/util/log.h"

namespace monger {
namespace {

HostAndPort makeHostAndPort(const ShardId& shardId) {
    return HostAndPort(str::stream() << shardId << ":123");
}

}  // namespace

void TransactionCoordinatorTestFixture::setUp() {
    ShardServerTestFixture::setUp();

    ASSERT_OK(ServerParameterSet::getGlobal()
                  ->getMap()
                  .find("logComponentVerbosity")
                  ->second->setFromString("{transaction: {verbosity: 3}}"));

    for (const auto& shardId : kThreeShardIdList) {
        auto shardTargeter = RemoteCommandTargeterMock::get(
            uassertStatusOK(shardRegistry()->getShard(operationContext(), shardId))->getTargeter());
        shardTargeter->setFindHostReturnValue(makeHostAndPort(shardId));
    }
}

std::unique_ptr<ShardingCatalogClient> TransactionCoordinatorTestFixture::makeShardingCatalogClient(
    std::unique_ptr<DistLockManager> distLockManager) {

    class StaticCatalogClient final : public ShardingCatalogClientMock {
    public:
        StaticCatalogClient(std::vector<ShardId> shardIds)
            : ShardingCatalogClientMock(nullptr), _shardIds(std::move(shardIds)) {}

        StatusWith<repl::OpTimeWith<std::vector<ShardType>>> getAllShards(
            OperationContext* opCtx, repl::ReadConcernLevel readConcern) override {
            std::vector<ShardType> shardTypes;
            for (const auto& shardId : _shardIds) {
                const ConnectionString cs =
                    ConnectionString::forReplicaSet(shardId.toString(), {makeHostAndPort(shardId)});
                ShardType sType;
                sType.setName(cs.getSetName());
                sType.setHost(cs.toString());
                shardTypes.push_back(std::move(sType));
            };
            return repl::OpTimeWith<std::vector<ShardType>>(shardTypes);
        }

    private:
        const std::vector<ShardId> _shardIds;
    };

    return std::make_unique<StaticCatalogClient>(kThreeShardIdList);
}

void TransactionCoordinatorTestFixture::assertCommandSentAndRespondWith(
    const StringData& commandName,
    const StatusWith<BSONObj>& response,
    boost::optional<BSONObj> expectedWriteConcern) {
    onCommand([&](const executor::RemoteCommandRequest& request) {
        ASSERT_EQ(commandName, request.cmdObj.firstElement().fieldNameStringData());
        if (expectedWriteConcern) {
            ASSERT_BSONOBJ_EQ(
                *expectedWriteConcern,
                request.cmdObj.getObjectField(WriteConcernOptions::kWriteConcernField));
        }
        return response;
    });
}

void TransactionCoordinatorTestFixture::advanceClockAndExecuteScheduledTasks() {
    executor::NetworkInterfaceMock::InNetworkGuard guard(network());
    network()->advanceTime(network()->now() + Seconds{1});
}

}  // namespace monger
