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

#include <memory>
#include <utility>

#include "monger/base/status.h"
#include "monger/base/status_with.h"
#include "monger/bson/json.h"
#include "monger/client/remote_command_targeter_factory_mock.h"
#include "monger/client/remote_command_targeter_mock.h"
#include "monger/s/client/shard_factory.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/client/shard_remote.h"
#include "monger/unittest/unittest.h"
#include "monger/util/time_support.h"

namespace monger {
namespace {

class ShardRegistryDataTest : public monger::unittest::Test {
public:
    ShardFactory* shardFactory() {
        return _shardFactory.get();
    }

private:
    void setUp() override {
        auto targeterFactory = std::make_unique<RemoteCommandTargeterFactoryMock>();
        auto targeterFactoryPtr = targeterFactory.get();

        ShardFactory::BuilderCallable setBuilder =
            [targeterFactoryPtr](const ShardId& shardId, const ConnectionString& connStr) {
                return std::make_unique<ShardRemote>(
                    shardId, connStr, targeterFactoryPtr->create(connStr));
            };

        ShardFactory::BuilderCallable masterBuilder =
            [targeterFactoryPtr](const ShardId& shardId, const ConnectionString& connStr) {
                return std::make_unique<ShardRemote>(
                    shardId, connStr, targeterFactoryPtr->create(connStr));
            };

        ShardFactory::BuildersMap buildersMap{
            {ConnectionString::SET, std::move(setBuilder)},
            {ConnectionString::MASTER, std::move(masterBuilder)},
        };

        _shardFactory =
            std::make_unique<ShardFactory>(std::move(buildersMap), std::move(targeterFactory));
    }

    void tearDown() override {}

    std::unique_ptr<ShardFactory> _shardFactory;
};


TEST_F(ShardRegistryDataTest, AddConfigShard) {
    ConnectionString configCS("rs/dummy1:1234,dummy2:2345,dummy3:3456", ConnectionString::SET);
    auto configShard = shardFactory()->createShard(ShardRegistry::kConfigServerShardId, configCS);

    ShardRegistryData data;
    data.addConfigShard(configShard);

    ASSERT_EQUALS(configCS.toString(), data.getConfigShard()->originalConnString().toString());
}

}  // unnamed namespace
}  // namespace monger
