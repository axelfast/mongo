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

#include "monger/s/sharding_mongerd_test_fixture.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "monger/base/checked_cast.h"
#include "monger/base/status_with.h"
#include "monger/client/remote_command_targeter_factory_mock.h"
#include "monger/client/remote_command_targeter_mock.h"
#include "monger/client/replica_set_monitor.h"
#include "monger/db/catalog_raii.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/namespace_string.h"
#include "monger/db/op_observer_registry.h"
#include "monger/db/query/cursor_response.h"
#include "monger/db/query/query_request.h"
#include "monger/db/repl/drop_pending_collection_reaper.h"
#include "monger/db/repl/oplog.h"
#include "monger/db/repl/read_concern_args.h"
#include "monger/db/repl/repl_settings.h"
#include "monger/db/repl/replication_consistency_markers_mock.h"
#include "monger/db/repl/replication_process.h"
#include "monger/db/repl/replication_recovery_mock.h"
#include "monger/db/repl/storage_interface_mock.h"
#include "monger/db/s/config_server_op_observer.h"
#include "monger/db/s/op_observer_sharding_impl.h"
#include "monger/db/s/shard_server_op_observer.h"
#include "monger/executor/network_interface_mock.h"
#include "monger/executor/task_executor_pool.h"
#include "monger/executor/thread_pool_task_executor_test_fixture.h"
#include "monger/rpc/metadata/repl_set_metadata.h"
#include "monger/s/balancer_configuration.h"
#include "monger/s/catalog/dist_lock_catalog.h"
#include "monger/s/catalog/dist_lock_manager.h"
#include "monger/s/catalog/sharding_catalog_client.h"
#include "monger/s/catalog/type_changelog.h"
#include "monger/s/catalog/type_collection.h"
#include "monger/s/catalog/type_shard.h"
#include "monger/s/catalog_cache.h"
#include "monger/s/catalog_cache_loader.h"
#include "monger/s/client/shard_factory.h"
#include "monger/s/client/shard_local.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/client/shard_remote.h"
#include "monger/s/grid.h"
#include "monger/s/query/cluster_cursor_manager.h"
#include "monger/s/request_types/set_shard_version_request.h"
#include "monger/util/clock_source_mock.h"
#include "monger/util/tick_source_mock.h"

namespace monger {

using executor::NetworkInterfaceMock;
using executor::NetworkTestEnv;
using executor::RemoteCommandRequest;
using executor::RemoteCommandResponse;
using repl::ReplicationCoordinator;
using repl::ReplicationCoordinatorMock;
using repl::ReplSettings;
using unittest::assertGet;

ShardingMongerdTestFixture::ShardingMongerdTestFixture() = default;

ShardingMongerdTestFixture::~ShardingMongerdTestFixture() = default;

void ShardingMongerdTestFixture::setUp() {
    ServiceContextMongerDTest::setUp();

    const auto service = getServiceContext();
    _opCtx = makeOperationContext();

    // Set up this node as part of a replica set.

    repl::ReplSettings replSettings;
    replSettings.setOplogSizeBytes(512'000);
    replSettings.setReplSetString(ConnectionString::forReplicaSet(_setName, _servers).toString());
    auto replCoordPtr = makeReplicationCoordinator(replSettings);
    _replCoord = replCoordPtr.get();

    BSONArrayBuilder serversBob;
    for (size_t i = 0; i < _servers.size(); ++i) {
        serversBob.append(BSON("host" << _servers[i].toString() << "_id" << static_cast<int>(i)));
    }
    repl::ReplSetConfig replSetConfig;
    ASSERT_OK(replSetConfig.initialize(
        BSON("_id" << _setName << "protocolVersion" << 1 << "version" << 3 << "members"
                   << serversBob.arr())));
    replCoordPtr->setGetConfigReturnValue(replSetConfig);

    repl::ReplicationCoordinator::set(service, std::move(replCoordPtr));

    auto storagePtr = std::make_unique<repl::StorageInterfaceMock>();

    repl::DropPendingCollectionReaper::set(
        service, std::make_unique<repl::DropPendingCollectionReaper>(storagePtr.get()));

    repl::ReplicationProcess::set(service,
                                  std::make_unique<repl::ReplicationProcess>(
                                      storagePtr.get(),
                                      std::make_unique<repl::ReplicationConsistencyMarkersMock>(),
                                      std::make_unique<repl::ReplicationRecoveryMock>()));

    ASSERT_OK(repl::ReplicationProcess::get(_opCtx.get())->initializeRollbackID(_opCtx.get()));

    repl::StorageInterface::set(service, std::move(storagePtr));

    auto opObserver = checked_cast<OpObserverRegistry*>(service->getOpObserver());
    opObserver->addObserver(std::make_unique<OpObserverShardingImpl>());
    opObserver->addObserver(std::make_unique<ConfigServerOpObserver>());
    opObserver->addObserver(std::make_unique<ShardServerOpObserver>());

    repl::setOplogCollectionName(service);
    repl::createOplog(_opCtx.get());

    // Set the highest FCV because otherwise it defaults to the lower FCV. This way we default to
    // testing this release's code, not backwards compatibility code.
    serverGlobalParams.featureCompatibility.setVersion(
        ServerGlobalParams::FeatureCompatibility::Version::kFullyUpgradedTo42);
}

std::unique_ptr<ReplicationCoordinatorMock> ShardingMongerdTestFixture::makeReplicationCoordinator(
    ReplSettings replSettings) {
    auto coordinator =
        std::make_unique<repl::ReplicationCoordinatorMock>(getServiceContext(), replSettings);
    ASSERT_OK(coordinator->setFollowerMode(repl::MemberState::RS_PRIMARY));
    return coordinator;
}

std::unique_ptr<executor::TaskExecutorPool> ShardingMongerdTestFixture::makeTaskExecutorPool() {
    // Set up a NetworkInterfaceMock. Note, unlike NetworkInterfaceASIO, which has its own pool of
    // threads, tasks in the NetworkInterfaceMock must be carried out synchronously by the (single)
    // thread the unit test is running on.
    auto netForFixedTaskExecutor = std::make_unique<executor::NetworkInterfaceMock>();
    _mockNetwork = netForFixedTaskExecutor.get();

    // Set up a ThreadPoolTaskExecutor. Note, for local tasks this TaskExecutor uses a
    // ThreadPoolMock, and for remote tasks it uses the NetworkInterfaceMock created above. However,
    // note that the ThreadPoolMock uses the NetworkInterfaceMock's threads to run tasks, which is
    // again just the (single) thread the unit test is running on. Therefore, all tasks, local and
    // remote, must be carried out synchronously by the test thread.
    auto fixedTaskExecutor = makeThreadPoolTestExecutor(std::move(netForFixedTaskExecutor));
    _networkTestEnv = std::make_unique<NetworkTestEnv>(fixedTaskExecutor.get(), _mockNetwork);

    // Set up (one) TaskExecutor for the set of arbitrary TaskExecutors.
    std::vector<std::unique_ptr<executor::TaskExecutor>> arbitraryExecutorsForExecutorPool;
    arbitraryExecutorsForExecutorPool.emplace_back(
        makeThreadPoolTestExecutor(std::make_unique<executor::NetworkInterfaceMock>()));

    // Set up the TaskExecutorPool with the fixed TaskExecutor and set of arbitrary TaskExecutors.
    auto executorPool = std::make_unique<executor::TaskExecutorPool>();
    executorPool->addExecutors(std::move(arbitraryExecutorsForExecutorPool),
                               std::move(fixedTaskExecutor));

    return executorPool;
}

std::unique_ptr<ShardRegistry> ShardingMongerdTestFixture::makeShardRegistry(
    ConnectionString configConnStr) {
    auto targeterFactory(std::make_unique<RemoteCommandTargeterFactoryMock>());
    auto targeterFactoryPtr = targeterFactory.get();
    _targeterFactory = targeterFactoryPtr;

    ShardFactory::BuilderCallable setBuilder = [targeterFactoryPtr](
        const ShardId& shardId, const ConnectionString& connStr) {
        return std::make_unique<ShardRemote>(shardId, connStr, targeterFactoryPtr->create(connStr));
    };

    ShardFactory::BuilderCallable masterBuilder = [targeterFactoryPtr](
        const ShardId& shardId, const ConnectionString& connStr) {
        return std::make_unique<ShardRemote>(shardId, connStr, targeterFactoryPtr->create(connStr));
    };

    ShardFactory::BuildersMap buildersMap{{ConnectionString::SET, std::move(setBuilder)},
                                          {ConnectionString::MASTER, std::move(masterBuilder)}};

    // Only config servers use ShardLocal for now.
    if (serverGlobalParams.clusterRole == ClusterRole::ConfigServer) {
        ShardFactory::BuilderCallable localBuilder = [](const ShardId& shardId,
                                                        const ConnectionString& connStr) {
            return std::make_unique<ShardLocal>(shardId);
        };
        buildersMap.insert(
            std::pair<ConnectionString::ConnectionType, ShardFactory::BuilderCallable>(
                ConnectionString::LOCAL, std::move(localBuilder)));
    }

    auto shardFactory =
        std::make_unique<ShardFactory>(std::move(buildersMap), std::move(targeterFactory));

    return std::make_unique<ShardRegistry>(std::move(shardFactory), configConnStr);
}

std::unique_ptr<DistLockCatalog> ShardingMongerdTestFixture::makeDistLockCatalog() {
    return nullptr;
}

std::unique_ptr<DistLockManager> ShardingMongerdTestFixture::makeDistLockManager(
    std::unique_ptr<DistLockCatalog> distLockCatalog) {
    return nullptr;
}

std::unique_ptr<ShardingCatalogClient> ShardingMongerdTestFixture::makeShardingCatalogClient(
    std::unique_ptr<DistLockManager> distLockManager) {
    return nullptr;
}

std::unique_ptr<ClusterCursorManager> ShardingMongerdTestFixture::makeClusterCursorManager() {
    return nullptr;
}

std::unique_ptr<BalancerConfiguration> ShardingMongerdTestFixture::makeBalancerConfiguration() {
    return nullptr;
}

Status ShardingMongerdTestFixture::initializeGlobalShardingStateForMongerdForTest(
    const ConnectionString& configConnStr) {
    invariant(serverGlobalParams.clusterRole == ClusterRole::ShardServer ||
              serverGlobalParams.clusterRole == ClusterRole::ConfigServer);

    // Create and initialize each sharding component individually before moving them to the Grid
    // in order to control the order of initialization, since some components depend on others.

    auto executorPoolPtr = makeTaskExecutorPool();
    if (executorPoolPtr) {
        executorPoolPtr->startup();
    }

    auto distLockCatalogPtr = makeDistLockCatalog();
    _distLockCatalog = distLockCatalogPtr.get();

    auto distLockManagerPtr = makeDistLockManager(std::move(distLockCatalogPtr));
    _distLockManager = distLockManagerPtr.get();

    auto const grid = Grid::get(operationContext());
    grid->init(makeShardingCatalogClient(std::move(distLockManagerPtr)),
               std::make_unique<CatalogCache>(CatalogCacheLoader::get(getServiceContext())),
               makeShardRegistry(configConnStr),
               makeClusterCursorManager(),
               makeBalancerConfiguration(),
               std::move(executorPoolPtr),
               _mockNetwork);

    // NOTE: ShardRegistry::startup() is not called because it starts a task executor with a
    // self-rescheduling task to reload the ShardRegistry over the network.
    // grid->shardRegistry()->startup();

    if (grid->catalogClient()) {
        grid->catalogClient()->startup();
    }

    return Status::OK();
}

void ShardingMongerdTestFixture::tearDown() {
    ReplicaSetMonitor::cleanup();

    if (Grid::get(operationContext())->getExecutorPool() && !_executorPoolShutDown) {
        Grid::get(operationContext())->getExecutorPool()->shutdownAndJoin();
    }

    if (Grid::get(operationContext())->catalogClient()) {
        Grid::get(operationContext())->catalogClient()->shutDown(operationContext());
    }

    if (Grid::get(operationContext())->shardRegistry()) {
        Grid::get(operationContext())->shardRegistry()->shutdown();
    }

    Grid::get(operationContext())->clearForUnitTests();

    _opCtx.reset();
    ServiceContextMongerDTest::tearDown();
}

ShardingCatalogClient* ShardingMongerdTestFixture::catalogClient() const {
    invariant(Grid::get(operationContext())->catalogClient());
    return Grid::get(operationContext())->catalogClient();
}

CatalogCache* ShardingMongerdTestFixture::catalogCache() const {
    invariant(Grid::get(operationContext())->catalogCache());
    return Grid::get(operationContext())->catalogCache();
}

ShardRegistry* ShardingMongerdTestFixture::shardRegistry() const {
    invariant(Grid::get(operationContext())->shardRegistry());
    return Grid::get(operationContext())->shardRegistry();
}

ClusterCursorManager* ShardingMongerdTestFixture::clusterCursorManager() const {
    invariant(Grid::get(operationContext())->getCursorManager());
    return Grid::get(operationContext())->getCursorManager();
}

executor::TaskExecutorPool* ShardingMongerdTestFixture::executorPool() const {
    invariant(Grid::get(operationContext())->getExecutorPool());
    return Grid::get(operationContext())->getExecutorPool();
}

void ShardingMongerdTestFixture::shutdownExecutorPool() {
    invariant(!_executorPoolShutDown);
    executorPool()->shutdownAndJoin();
    _executorPoolShutDown = true;
}

executor::TaskExecutor* ShardingMongerdTestFixture::executor() const {
    invariant(Grid::get(operationContext())->getExecutorPool());
    return Grid::get(operationContext())->getExecutorPool()->getFixedExecutor();
}

repl::ReplicationCoordinatorMock* ShardingMongerdTestFixture::replicationCoordinator() const {
    invariant(_replCoord);
    return _replCoord;
}

DistLockCatalog* ShardingMongerdTestFixture::distLockCatalog() const {
    invariant(_distLockCatalog);
    return _distLockCatalog;
}

DistLockManager* ShardingMongerdTestFixture::distLock() const {
    invariant(_distLockManager);
    return _distLockManager;
}

RemoteCommandTargeterFactoryMock* ShardingMongerdTestFixture::targeterFactory() const {
    invariant(_targeterFactory);
    return _targeterFactory;
}

}  // namespace monger
