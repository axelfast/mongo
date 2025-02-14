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

#include "monger/db/s/sharding_state.h"

#include "monger/db/operation_context.h"
#include "monger/db/server_options.h"
#include "monger/util/log.h"

namespace monger {
namespace {

const auto getShardingState = ServiceContext::declareDecoration<ShardingState>();

}  // namespace

ShardingState::ShardingState() = default;

ShardingState::~ShardingState() = default;

ShardingState* ShardingState::get(ServiceContext* serviceContext) {
    return &getShardingState(serviceContext);
}

ShardingState* ShardingState::get(OperationContext* operationContext) {
    return ShardingState::get(operationContext->getServiceContext());
}

void ShardingState::setInitialized(ShardId shardId, OID clusterId) {
    stdx::unique_lock<stdx::mutex> ul(_mutex);
    invariant(_getInitializationState() == InitializationState::kNew);

    _shardId = std::move(shardId);
    _clusterId = std::move(clusterId);
    _initializationStatus = Status::OK();

    _initializationState.store(static_cast<uint32_t>(InitializationState::kInitialized));
}

void ShardingState::setInitialized(Status failedStatus) {
    invariant(!failedStatus.isOK());
    log() << "Failed to initialize sharding components" << causedBy(failedStatus);

    stdx::unique_lock<stdx::mutex> ul(_mutex);
    invariant(_getInitializationState() == InitializationState::kNew);

    _initializationStatus = std::move(failedStatus);
    _initializationState.store(static_cast<uint32_t>(InitializationState::kError));
}

boost::optional<Status> ShardingState::initializationStatus() {
    stdx::unique_lock<stdx::mutex> ul(_mutex);
    if (_getInitializationState() == InitializationState::kNew)
        return boost::none;

    return _initializationStatus;
}

bool ShardingState::enabled() const {
    return _getInitializationState() == InitializationState::kInitialized;
}

Status ShardingState::canAcceptShardedCommands() const {
    if (serverGlobalParams.clusterRole != ClusterRole::ShardServer) {
        return {ErrorCodes::NoShardingEnabled,
                "Cannot accept sharding commands if not started with --shardsvr"};
    } else if (!enabled()) {
        return {ErrorCodes::ShardingStateNotInitialized,
                "Cannot accept sharding commands if sharding state has not "
                "been initialized with a shardIdentity document"};
    } else {
        return Status::OK();
    }
}

ShardId ShardingState::shardId() {
    invariant(enabled());
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    return _shardId;
}

OID ShardingState::clusterId() {
    invariant(enabled());
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    return _clusterId;
}

void ShardingState::clearForTests() {
    _initializationState.store(static_cast<uint32_t>(InitializationState::kNew));
}

}  // namespace monger
