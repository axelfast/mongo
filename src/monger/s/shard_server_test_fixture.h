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

#pragma once

#include "monger/db/server_options.h"
#include "monger/s/sharding_mongerd_test_fixture.h"

namespace monger {

class RemoteCommandTargeterMock;

/**
 * Test fixture for shard components, as opposed to config or mongers components. Provides a mock
 * network and ephemeral storage engine via ShardingMongerdTestFixture. Additionally sets up mock
 * dist lock catalog and manager with a real catalog client.
 */
class ShardServerTestFixture : public ShardingMongerdTestFixture {
public:
    ShardServerTestFixture();
    ~ShardServerTestFixture();

    /**
     * Returns the mock targeter for the config server. Useful to use like so,
     *
     *     configTargeterMock()->setFindHostReturnValue(HostAndPort);
     *     configTargeterMock()->setFindHostReturnValue({ErrorCodes::InternalError, "can't target"})
     *
     * Remote calls always need to resolve a host with RemoteCommandTargeterMock::findHost, so it
     * must be set.
     */
    std::shared_ptr<RemoteCommandTargeterMock> configTargeterMock();

protected:
    void setUp() override;

    void tearDown() override;

    /**
     * Creates a DistLockCatalogMock.
     */
    std::unique_ptr<DistLockCatalog> makeDistLockCatalog() override;

    /**
     * Creates a DistLockManagerMock.
     */
    std::unique_ptr<DistLockManager> makeDistLockManager(
        std::unique_ptr<DistLockCatalog> distLockCatalog) override;

    /**
     * Creates a real ShardingCatalogClient.
     */
    std::unique_ptr<ShardingCatalogClient> makeShardingCatalogClient(
        std::unique_ptr<DistLockManager> distLockManager) override;

    const ShardId _myShardName{"myShardName"};
    OID _clusterId;
};

}  // namespace monger
