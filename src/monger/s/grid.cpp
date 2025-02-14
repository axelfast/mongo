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

#include "monger/s/grid.h"

#include "monger/db/operation_context.h"
#include "monger/db/server_options.h"
#include "monger/executor/task_executor.h"
#include "monger/executor/task_executor_pool.h"
#include "monger/s/balancer_configuration.h"
#include "monger/s/catalog_cache.h"
#include "monger/s/client/shard_factory.h"
#include "monger/s/query/cluster_cursor_manager.h"
#include "monger/util/log.h"

namespace monger {
namespace {
const auto grid = ServiceContext::declareDecoration<Grid>();
}  // namespace

Grid::Grid() = default;

Grid::~Grid() = default;

Grid* Grid::get(ServiceContext* serviceContext) {
    return &grid(serviceContext);
}

Grid* Grid::get(OperationContext* operationContext) {
    return get(operationContext->getServiceContext());
}

void Grid::init(std::unique_ptr<ShardingCatalogClient> catalogClient,
                std::unique_ptr<CatalogCache> catalogCache,
                std::unique_ptr<ShardRegistry> shardRegistry,
                std::unique_ptr<ClusterCursorManager> cursorManager,
                std::unique_ptr<BalancerConfiguration> balancerConfig,
                std::unique_ptr<executor::TaskExecutorPool> executorPool,
                executor::NetworkInterface* network) {
    invariant(!_catalogClient);
    invariant(!_catalogCache);
    invariant(!_shardRegistry);
    invariant(!_cursorManager);
    invariant(!_balancerConfig);
    invariant(!_executorPool);
    invariant(!_network);

    _catalogClient = std::move(catalogClient);
    _catalogCache = std::move(catalogCache);
    _shardRegistry = std::move(shardRegistry);
    _cursorManager = std::move(cursorManager);
    _balancerConfig = std::move(balancerConfig);
    _executorPool = std::move(executorPool);
    _network = network;

    _shardRegistry->init();
}

bool Grid::isShardingInitialized() const {
    return _shardingInitialized.load();
}

void Grid::setShardingInitialized() {
    invariant(!_shardingInitialized.load());
    _shardingInitialized.store(true);
}

Grid::CustomConnectionPoolStatsFn Grid::getCustomConnectionPoolStatsFn() const {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    return _customConnectionPoolStatsFn;
}

void Grid::setCustomConnectionPoolStatsFn(CustomConnectionPoolStatsFn statsFn) {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    invariant(!_customConnectionPoolStatsFn || !statsFn);
    _customConnectionPoolStatsFn = std::move(statsFn);
}

bool Grid::allowLocalHost() const {
    return _allowLocalShard;
}

void Grid::setAllowLocalHost(bool allow) {
    _allowLocalShard = allow;
}

repl::OpTime Grid::configOpTime() const {
    invariant(serverGlobalParams.clusterRole != ClusterRole::ConfigServer);

    stdx::lock_guard<stdx::mutex> lk(_mutex);
    return _configOpTime;
}

boost::optional<repl::OpTime> Grid::advanceConfigOpTime(OperationContext* opCtx,
                                                        repl::OpTime opTime,
                                                        StringData what) {
    const auto prevOpTime = _advanceConfigOpTime(opTime);
    if (prevOpTime && prevOpTime->getTerm() != opTime.getTerm()) {
        std::string clientAddr = "(unknown)";
        if (opCtx && opCtx->getClient()) {
            clientAddr = opCtx->getClient()->clientAddress(true);
        }
        log() << "Received " << what << " " << clientAddr << " indicating config server optime "
                                                             "term has increased, previous optime "
              << prevOpTime << ", now " << opTime;
    }
    return prevOpTime;
}

boost::optional<repl::OpTime> Grid::_advanceConfigOpTime(const repl::OpTime& opTime) {
    invariant(serverGlobalParams.clusterRole != ClusterRole::ConfigServer);

    stdx::lock_guard<stdx::mutex> lk(_mutex);
    if (_configOpTime < opTime) {
        repl::OpTime prev = _configOpTime;
        _configOpTime = opTime;
        return prev;
    }
    return boost::none;
}

void Grid::clearForUnitTests() {
    _catalogClient.reset();
    _catalogCache.reset();
    _shardRegistry.reset();
    _cursorManager.reset();
    _balancerConfig.reset();
    _executorPool.reset();
    _network = nullptr;

    _configOpTime = repl::OpTime();
}

}  // namespace monger
