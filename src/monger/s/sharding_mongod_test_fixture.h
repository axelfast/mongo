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

#include "monger/db/repl/replication_coordinator_mock.h"
#include "monger/db/service_context_d_test_fixture.h"
#include "monger/s/sharding_test_fixture_common.h"

namespace monger {

class CatalogCacheLoader;
class ConnectionString;
class DistLockCatalog;
class DistLockManager;
class RemoteCommandTargeterFactoryMock;

namespace repl {
class ReplSettings;
}  // namespace repl

/**
 * Sets up this fixture as a mongerd with a storage engine, OpObserver, and as a member of a replica
 * set.
 *
 * Additionally, provides an interface for initializing sharding components, mimicking the process
 * by which a real config or shard server does sharding initialization. Provides a set of default
 * components (including a NetworkInterface/TaskExecutor subsystem backed by the NetworkTestEnv),
 * but allows subclasses to replace any component with its real implementation, a mock, or nullptr.
 */
class ShardingMongerdTestFixture : public ServiceContextMongerDTest,
                                  public ShardingTestFixtureCommon {
public:
    ShardingMongerdTestFixture();
    ~ShardingMongerdTestFixture();

    /**
     * Initializes sharding components according to the cluster role in
     * serverGlobalParams.clusterRole and puts the components on the Grid, mimicking the
     * initialization done by an actual config or shard mongerd server.
     *
     * It is illegal to call this if serverGlobalParams.clusterRole is not ClusterRole::ShardServer
     * or ClusterRole::ConfigServer.
     */
    Status initializeGlobalShardingStateForMongerdForTest(const ConnectionString& configConnStr);

    // Syntactic sugar for getting sharding components off the Grid, if they have been initialized.

    ShardingCatalogClient* catalogClient() const;
    CatalogCache* catalogCache() const;
    ShardRegistry* shardRegistry() const;
    RemoteCommandTargeterFactoryMock* targeterFactory() const;
    executor::TaskExecutor* executor() const;
    DistLockManager* distLock() const;
    ClusterCursorManager* clusterCursorManager() const;
    executor::TaskExecutorPool* executorPool() const;

    /**
     * Shuts down the TaskExecutorPool and remembers that it has been shut down, so that it is not
     * shut down again on tearDown.
     *
     * Not safe to call from multiple threads.
     */
    void shutdownExecutorPool();

    repl::ReplicationCoordinatorMock* replicationCoordinator() const;

    /**
     * Returns the stored raw pointer to the DistLockCatalog, if it has been initialized.
     */
    DistLockCatalog* distLockCatalog() const;

    /**
     * Returns the stored raw pointer to the OperationContext.
     */
    OperationContext* operationContext() const {
        return _opCtx.get();
    }

protected:
    /**
     * Sets up this fixture with a storage engine, OpObserver, and as a member of a replica set.
     */
    void setUp() override;

    /**
     * Resets the storage engine and operation context, and shuts down and resets any sharding
     * components that have been initialized but not yet shut down and reset.
     */
    void tearDown() override;

    // Methods for creating and returning sharding components. Some of these methods have been
    // implemented to return the real implementation of the component as the default, while others
    // return a mock or nullptr. Subclasses can override any of these methods to create and
    // initialize a real implementation, a mock, or nullptr, as needed.

    // Warning: If a component takes ownership of another component for which a real or mock is
    // being used, the component must also be real or mock implementation, so that it can actually
    // take the ownership.

    /**
     * Base class returns ReplicationCoordinatorMock.
     */
    virtual std::unique_ptr<repl::ReplicationCoordinatorMock> makeReplicationCoordinator(
        repl::ReplSettings replSettings);

    /**
     * Base class returns a TaskExecutorPool with a fixed TaskExecutor and a set of arbitrary
     * executors containing one TaskExecutor, each backed by a NetworkInterfaceMock/ThreadPoolMock
     * subsytem.
     */
    virtual std::unique_ptr<executor::TaskExecutorPool> makeTaskExecutorPool();

    /**
     * Base class returns a real implementation of ShardRegistry.
     */
    virtual std::unique_ptr<ShardRegistry> makeShardRegistry(ConnectionString configConnStr);

    /**
     * Base class returns nullptr.
     */
    virtual std::unique_ptr<DistLockCatalog> makeDistLockCatalog();

    /**
     * Base class returns nullptr.
     *
     * Note: DistLockManager takes ownership of the DistLockCatalog, so if DistLockCatalog is not
     * nullptr, a real or mock DistLockManager must be supplied.
     */
    virtual std::unique_ptr<DistLockManager> makeDistLockManager(
        std::unique_ptr<DistLockCatalog> distLockCatalog);

    /**
     * Base class returns nullptr.
     *
     * Note: ShardingCatalogClient takes ownership of DistLockManager, so if DistLockManager is not
     * nulllptr, a real or mock ShardingCatalogClient must be supplied.
     */
    virtual std::unique_ptr<ShardingCatalogClient> makeShardingCatalogClient(
        std::unique_ptr<DistLockManager> distLockManager);

    /**
     * Base class returns nullptr.
     */
    virtual std::unique_ptr<ClusterCursorManager> makeClusterCursorManager();

    /**
     * Base class returns nullptr.
     */
    virtual std::unique_ptr<BalancerConfiguration> makeBalancerConfiguration();

private:
    const HostAndPort _host{"node1:12345"};
    const std::string _setName = "mySet";
    const std::vector<HostAndPort> _servers{
        _host, HostAndPort("node2:12345"), HostAndPort("node3:12345")};

    ServiceContext::UniqueOperationContext _opCtx;

    // Since the RemoteCommandTargeterFactory is currently a private member of ShardFactory, we
    // store a raw pointer to it here.
    RemoteCommandTargeterFactoryMock* _targeterFactory = nullptr;

    // Since the DistLockCatalog is currently a private member of ReplSetDistLockManager, we store
    // a raw pointer to it here.
    DistLockCatalog* _distLockCatalog = nullptr;

    // Since the DistLockManager is currently a private member of ShardingCatalogClient, we
    // store a raw pointer to it here.
    DistLockManager* _distLockManager = nullptr;

    repl::ReplicationCoordinatorMock* _replCoord = nullptr;

    // Records if a component has been shut down, so that it is only shut down once.
    bool _executorPoolShutDown = false;
};

}  // namespace monger
