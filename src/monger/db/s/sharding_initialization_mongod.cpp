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

#include "monger/db/s/sharding_initialization_mongerd.h"

#include "monger/client/connection_string.h"
#include "monger/client/global_conn_pool.h"
#include "monger/client/remote_command_targeter.h"
#include "monger/client/remote_command_targeter_factory_impl.h"
#include "monger/client/replica_set_monitor.h"
#include "monger/db/catalog_raii.h"
#include "monger/db/concurrency/d_concurrency.h"
#include "monger/db/dbhelpers.h"
#include "monger/db/logical_time_metadata_hook.h"
#include "monger/db/logical_time_validator.h"
#include "monger/db/operation_context.h"
#include "monger/db/ops/update.h"
#include "monger/db/repl/replication_coordinator.h"
#include "monger/db/s/chunk_splitter.h"
#include "monger/db/s/periodic_balancer_config_refresher.h"
#include "monger/db/s/read_only_catalog_cache_loader.h"
#include "monger/db/s/shard_server_catalog_cache_loader.h"
#include "monger/db/s/sharding_config_optime_gossip.h"
#include "monger/db/s/transaction_coordinator_service.h"
#include "monger/db/server_options.h"
#include "monger/executor/task_executor_pool.h"
#include "monger/rpc/metadata/egress_metadata_hook_list.h"
#include "monger/s/catalog_cache.h"
#include "monger/s/client/shard_connection.h"
#include "monger/s/client/shard_factory.h"
#include "monger/s/client/shard_local.h"
#include "monger/s/client/shard_remote.h"
#include "monger/s/client/sharding_connection_hook.h"
#include "monger/s/config_server_catalog_cache_loader.h"
#include "monger/s/grid.h"
#include "monger/s/sharding_initialization.h"
#include "monger/util/log.h"

namespace monger {

// Failpoint for disabling updateShardIdentityConfigString calls on signaled RS nodes.
MONGO_FAIL_POINT_DEFINE(failUpdateShardIdentityConfigString);

namespace {

const auto getInstance = ServiceContext::declareDecoration<ShardingInitializationMongerD>();

auto makeEgressHooksList(ServiceContext* service) {
    auto unshardedHookList = std::make_unique<rpc::EgressMetadataHookList>();
    unshardedHookList->addHook(std::make_unique<rpc::LogicalTimeMetadataHook>(service));
    unshardedHookList->addHook(std::make_unique<rpc::ShardingEgressMetadataHookForMongerd>(service));

    return unshardedHookList;
}

/**
 * Updates the config server field of the shardIdentity document with the given connection string if
 * setName is equal to the config server replica set name.
 */
class ShardingReplicaSetChangeListener final : public ReplicaSetChangeNotifier::Listener {
public:
    ShardingReplicaSetChangeListener(ServiceContext* serviceContext)
        : _serviceContext(serviceContext) {}
    ~ShardingReplicaSetChangeListener() final = default;

    void onFoundSet(const Key&) final {}

    // Update the shard identy config string
    void onConfirmedSet(const State& state) final {
        Grid::get(_serviceContext)->getExecutorPool()->getFixedExecutor()->schedule([
            serviceContext = _serviceContext,
            connStr = state.connStr
        ](Status status) {
            if (ErrorCodes::isCancelationError(status.code())) {
                LOG(2) << "Unable to schedule confirmed set update due to " << status;
                return;
            }
            uassertStatusOK(status);

            LOG(0) << "Updating config server with confirmed set " << connStr;
            Grid::get(serviceContext)->shardRegistry()->updateReplSetHosts(connStr);

            if (MONGO_FAIL_POINT(failUpdateShardIdentityConfigString)) {
                return;
            }

            auto configsvrConnStr =
                Grid::get(serviceContext)->shardRegistry()->getConfigServerConnectionString();

            // Only proceed if the notification is for the configsvr
            if (configsvrConnStr.getSetName() != connStr.getSetName()) {
                return;
            }

            ThreadClient tc("updateShardIdentityConfigString", serviceContext);
            auto opCtx = tc->makeOperationContext();

            ShardingInitializationMongerD::updateShardIdentityConfigString(opCtx.get(), connStr);
        });
    }
    void onPossibleSet(const State& state) final {
        Grid::get(_serviceContext)->shardRegistry()->updateReplSetHosts(state.connStr);
    }
    void onDroppedSet(const Key&) final {}

private:
    ServiceContext* _serviceContext;
};

}  // namespace

void ShardingInitializationMongerD::initializeShardingEnvironmentOnShardServer(
    OperationContext* opCtx, const ShardIdentity& shardIdentity, StringData distLockProcessId) {
    initializeGlobalShardingStateForMongerD(
        opCtx, shardIdentity.getConfigsvrConnectionString(), distLockProcessId);

    _replicaSetChangeListener =
        ReplicaSetMonitor::getNotifier().makeListener<ShardingReplicaSetChangeListener>(
            opCtx->getServiceContext());

    // Determine primary/secondary/standalone state in order to properly initialize sharding
    // components.
    const auto replCoord = repl::ReplicationCoordinator::get(opCtx);
    bool isReplSet = replCoord->getReplicationMode() == repl::ReplicationCoordinator::modeReplSet;
    bool isStandaloneOrPrimary =
        !isReplSet || (replCoord->getMemberState() == repl::MemberState::RS_PRIMARY);

    CatalogCacheLoader::get(opCtx).initializeReplicaSetRole(isStandaloneOrPrimary);
    ChunkSplitter::get(opCtx).onShardingInitialization(isStandaloneOrPrimary);
    PeriodicBalancerConfigRefresher::get(opCtx).onShardingInitialization(opCtx->getServiceContext(),
                                                                         isStandaloneOrPrimary);

    // Start the transaction coordinator service only if the node is the primary of a replica set
    TransactionCoordinatorService::get(opCtx)->onShardingInitialization(
        opCtx, isReplSet && isStandaloneOrPrimary);

    Grid::get(opCtx)->setShardingInitialized();

    LOG(0) << "Finished initializing sharding components for "
           << (isStandaloneOrPrimary ? "primary" : "secondary") << " node.";
}

ShardingInitializationMongerD::ShardingInitializationMongerD()
    : _initFunc([this](auto... args) {
          this->initializeShardingEnvironmentOnShardServer(std::forward<decltype(args)>(args)...);
      }) {}

ShardingInitializationMongerD::~ShardingInitializationMongerD() = default;

ShardingInitializationMongerD* ShardingInitializationMongerD::get(OperationContext* opCtx) {
    return get(opCtx->getServiceContext());
}

ShardingInitializationMongerD* ShardingInitializationMongerD::get(ServiceContext* service) {
    return &getInstance(service);
}

void ShardingInitializationMongerD::shutDown(OperationContext* opCtx) {
    auto const shardingState = ShardingState::get(opCtx);
    auto const grid = Grid::get(opCtx);

    if (!shardingState->enabled())
        return;

    grid->getExecutorPool()->shutdownAndJoin();
    grid->catalogClient()->shutDown(opCtx);
    grid->shardRegistry()->shutdown();
    _replicaSetChangeListener.reset();
}

bool ShardingInitializationMongerD::initializeShardingAwarenessIfNeeded(OperationContext* opCtx) {
    invariant(!opCtx->lockState()->isLocked());

    // In sharded readOnly mode, we ignore the shardIdentity document on disk and instead *require*
    // a shardIdentity document to be passed through --overrideShardIdentity
    if (storageGlobalParams.readOnly) {
        if (serverGlobalParams.clusterRole == ClusterRole::ShardServer) {
            uassert(ErrorCodes::InvalidOptions,
                    "If started with --shardsvr in queryableBackupMode, a shardIdentity document "
                    "must be provided through --overrideShardIdentity",
                    !serverGlobalParams.overrideShardIdentity.isEmpty());

            auto overrideShardIdentity =
                uassertStatusOK(ShardIdentityType::fromShardIdentityDocument(
                    serverGlobalParams.overrideShardIdentity));

            {
                // Global lock is required to call initializeFromShardIdentity
                Lock::GlobalWrite lk(opCtx);
                initializeFromShardIdentity(opCtx, overrideShardIdentity);
            }

            return true;
        } else {
            // Error if --overrideShardIdentity is used but *not* started with --shardsvr
            uassert(ErrorCodes::InvalidOptions,
                    str::stream()
                        << "Not started with --shardsvr, but a shardIdentity document was provided "
                           "through --overrideShardIdentity: "
                        << serverGlobalParams.overrideShardIdentity,
                    serverGlobalParams.overrideShardIdentity.isEmpty());
            return false;
        }

        MONGO_UNREACHABLE;
    }

    // In sharded *non*-readOnly mode, error if --overrideShardIdentity is provided
    uassert(ErrorCodes::InvalidOptions,
            str::stream() << "--overrideShardIdentity is only allowed in sharded "
                             "queryableBackupMode. If not in queryableBackupMode, you can edit "
                             "the shardIdentity document by starting the server *without* "
                             "--shardsvr, manually updating the shardIdentity document in the "
                          << NamespaceString::kServerConfigurationNamespace.toString()
                          << " collection, and restarting the server with --shardsvr.",
            serverGlobalParams.overrideShardIdentity.isEmpty());

    // Use the shardIdentity document on disk if one exists, but it is okay if no shardIdentity
    // document is available at all (sharding awareness will be initialized when a shardIdentity
    // document is inserted)
    BSONObj shardIdentityBSON;
    const bool foundShardIdentity = [&] {
        AutoGetCollection autoColl(opCtx, NamespaceString::kServerConfigurationNamespace, MODE_IS);
        return Helpers::findOne(opCtx,
                                autoColl.getCollection(),
                                BSON("_id" << ShardIdentityType::IdName),
                                shardIdentityBSON);
    }();

    if (serverGlobalParams.clusterRole == ClusterRole::ShardServer) {
        if (!foundShardIdentity) {
            warning() << "Started with --shardsvr, but no shardIdentity document was found on "
                         "disk in "
                      << NamespaceString::kServerConfigurationNamespace
                      << ". This most likely means this server has not yet been added to a "
                         "sharded cluster.";
            return false;
        }

        invariant(!shardIdentityBSON.isEmpty());

        auto shardIdentity =
            uassertStatusOK(ShardIdentityType::fromShardIdentityDocument(shardIdentityBSON));

        {
            // Global lock is required to call initializeFromShardIdentity
            Lock::GlobalWrite lk(opCtx);
            initializeFromShardIdentity(opCtx, shardIdentity);
        }

        return true;
    } else {
        // Warn if a shardIdentity document is found on disk but *not* started with --shardsvr.
        if (!shardIdentityBSON.isEmpty()) {
            warning() << "Not started with --shardsvr, but a shardIdentity document was found "
                         "on disk in "
                      << NamespaceString::kServerConfigurationNamespace << ": "
                      << shardIdentityBSON;
        }
        return false;
    }
}

void ShardingInitializationMongerD::initializeFromShardIdentity(
    OperationContext* opCtx, const ShardIdentityType& shardIdentity) {
    invariant(serverGlobalParams.clusterRole == ClusterRole::ShardServer);
    invariant(opCtx->lockState()->isLocked());

    uassertStatusOKWithContext(
        shardIdentity.validate(),
        "Invalid shard identity document found when initializing sharding state");

    log() << "initializing sharding state with: " << shardIdentity;

    const auto& configSvrConnStr = shardIdentity.getConfigsvrConnectionString();

    auto const shardingState = ShardingState::get(opCtx);
    auto const shardRegistry = Grid::get(opCtx)->shardRegistry();

    stdx::unique_lock<stdx::mutex> ul(_initSynchronizationMutex);

    if (shardingState->enabled()) {
        uassert(40371, "", shardingState->shardId() == shardIdentity.getShardName());
        uassert(40372, "", shardingState->clusterId() == shardIdentity.getClusterId());

        auto prevConfigsvrConnStr = shardRegistry->getConfigServerConnectionString();
        uassert(40373, "", prevConfigsvrConnStr.type() == ConnectionString::SET);
        uassert(40374, "", prevConfigsvrConnStr.getSetName() == configSvrConnStr.getSetName());

        return;
    }

    auto initializationStatus = shardingState->initializationStatus();
    uassert(ErrorCodes::ManualInterventionRequired,
            str::stream() << "Server's sharding metadata manager failed to initialize and will "
                             "remain in this state until the instance is manually reset"
                          << causedBy(*initializationStatus),
            !initializationStatus);

    try {
        _initFunc(opCtx, shardIdentity, generateDistLockProcessId(opCtx));
        shardingState->setInitialized(shardIdentity.getShardName().toString(),
                                      shardIdentity.getClusterId());
    } catch (const DBException& ex) {
        shardingState->setInitialized(ex.toStatus());
    }
}

void ShardingInitializationMongerD::updateShardIdentityConfigString(
    OperationContext* opCtx, const ConnectionString& newConnectionString) {
    BSONObj updateObj(
        ShardIdentityType::createConfigServerUpdateObject(newConnectionString.toString()));

    UpdateRequest updateReq(NamespaceString::kServerConfigurationNamespace);
    updateReq.setQuery(BSON("_id" << ShardIdentityType::IdName));
    updateReq.setUpdateModification(updateObj);

    try {
        AutoGetOrCreateDb autoDb(
            opCtx, NamespaceString::kServerConfigurationNamespace.db(), MODE_X);

        auto result = update(opCtx, autoDb.getDb(), updateReq);
        if (result.numMatched == 0) {
            warning() << "failed to update config string of shard identity document because "
                      << "it does not exist. This shard could have been removed from the cluster";
        } else {
            LOG(2) << "Updated config server connection string in shardIdentity document to"
                   << newConnectionString;
        }
    } catch (const DBException& exception) {
        auto status = exception.toStatus();
        if (!ErrorCodes::isNotMasterError(status.code())) {
            warning() << "Error encountered while trying to update config connection string to "
                      << newConnectionString.toString() << causedBy(redact(status));
        }
    }
}

void initializeGlobalShardingStateForMongerD(OperationContext* opCtx,
                                            const ConnectionString& configCS,
                                            StringData distLockProcessId) {
    auto targeterFactory = std::make_unique<RemoteCommandTargeterFactoryImpl>();
    auto targeterFactoryPtr = targeterFactory.get();

    ShardFactory::BuilderCallable setBuilder = [targeterFactoryPtr](
        const ShardId& shardId, const ConnectionString& connStr) {
        return std::make_unique<ShardRemote>(shardId, connStr, targeterFactoryPtr->create(connStr));
    };

    ShardFactory::BuilderCallable masterBuilder = [targeterFactoryPtr](
        const ShardId& shardId, const ConnectionString& connStr) {
        return std::make_unique<ShardRemote>(shardId, connStr, targeterFactoryPtr->create(connStr));
    };

    ShardFactory::BuilderCallable localBuilder = [](const ShardId& shardId,
                                                    const ConnectionString& connStr) {
        return std::make_unique<ShardLocal>(shardId);
    };

    ShardFactory::BuildersMap buildersMap{
        {ConnectionString::SET, std::move(setBuilder)},
        {ConnectionString::MASTER, std::move(masterBuilder)},
        {ConnectionString::LOCAL, std::move(localBuilder)},
    };

    auto shardFactory =
        std::make_unique<ShardFactory>(std::move(buildersMap), std::move(targeterFactory));

    auto const service = opCtx->getServiceContext();

    if (serverGlobalParams.clusterRole == ClusterRole::ShardServer) {
        if (storageGlobalParams.readOnly) {
            CatalogCacheLoader::set(service, std::make_unique<ReadOnlyCatalogCacheLoader>());
        } else {
            CatalogCacheLoader::set(service,
                                    std::make_unique<ShardServerCatalogCacheLoader>(
                                        std::make_unique<ConfigServerCatalogCacheLoader>()));
        }
    } else {
        CatalogCacheLoader::set(service, std::make_unique<ConfigServerCatalogCacheLoader>());
    }

    auto validator = LogicalTimeValidator::get(service);
    if (validator) {  // The keyManager may be existing if the node was a part of a standalone RS.
        validator->stopKeyManager();
    }

    globalConnPool.addHook(new ShardingConnectionHook(false, makeEgressHooksList(service)));
    shardConnectionPool.addHook(new ShardingConnectionHook(true, makeEgressHooksList(service)));

    uassertStatusOK(initializeGlobalShardingState(
        opCtx,
        configCS,
        distLockProcessId,
        std::move(shardFactory),
        std::make_unique<CatalogCache>(CatalogCacheLoader::get(opCtx)),
        [service] { return makeEgressHooksList(service); },
        // We only need one task executor here because sharding task executors aren't used for user
        // queries in mongerd.
        1));

    auto const replCoord = repl::ReplicationCoordinator::get(service);
    if (serverGlobalParams.clusterRole == ClusterRole::ConfigServer &&
        replCoord->getMemberState().primary()) {
        LogicalTimeValidator::get(opCtx)->enableKeyGenerator(opCtx, true);
    }
}

}  // namespace monger
