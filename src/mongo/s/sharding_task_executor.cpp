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

#define MONGO_LOG_DEFAULT_COMPONENT monger::logger::LogComponent::kSharding

#include "monger/platform/basic.h"

#include "monger/s/sharding_task_executor.h"

#include "monger/base/status_with.h"
#include "monger/bson/timestamp.h"
#include "monger/db/logical_time.h"
#include "monger/db/operation_time_tracker.h"
#include "monger/executor/thread_pool_task_executor.h"
#include "monger/rpc/get_status_from_command_result.h"
#include "monger/rpc/metadata/sharding_metadata.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/cluster_last_error_info.h"
#include "monger/s/grid.h"
#include "monger/s/is_mongers.h"
#include "monger/s/transaction_router.h"
#include "monger/util/log.h"
#include "monger/util/scopeguard.h"

namespace monger {
namespace executor {

namespace {
const std::string kOperationTimeField = "operationTime";
}

ShardingTaskExecutor::ShardingTaskExecutor(std::unique_ptr<ThreadPoolTaskExecutor> executor)
    : _executor(std::move(executor)) {}

void ShardingTaskExecutor::startup() {
    _executor->startup();
}

void ShardingTaskExecutor::shutdown() {
    _executor->shutdown();
}

void ShardingTaskExecutor::join() {
    _executor->join();
}

void ShardingTaskExecutor::appendDiagnosticBSON(monger::BSONObjBuilder* builder) const {
    _executor->appendDiagnosticBSON(builder);
}

Date_t ShardingTaskExecutor::now() {
    return _executor->now();
}

StatusWith<TaskExecutor::EventHandle> ShardingTaskExecutor::makeEvent() {
    return _executor->makeEvent();
}

void ShardingTaskExecutor::signalEvent(const EventHandle& event) {
    return _executor->signalEvent(event);
}

StatusWith<TaskExecutor::CallbackHandle> ShardingTaskExecutor::onEvent(const EventHandle& event,
                                                                       CallbackFn&& work) {
    return _executor->onEvent(event, std::move(work));
}

void ShardingTaskExecutor::waitForEvent(const EventHandle& event) {
    _executor->waitForEvent(event);
}

StatusWith<stdx::cv_status> ShardingTaskExecutor::waitForEvent(OperationContext* opCtx,
                                                               const EventHandle& event,
                                                               Date_t deadline) {
    return _executor->waitForEvent(opCtx, event, deadline);
}

StatusWith<TaskExecutor::CallbackHandle> ShardingTaskExecutor::scheduleWork(CallbackFn&& work) {
    return _executor->scheduleWork(std::move(work));
}

StatusWith<TaskExecutor::CallbackHandle> ShardingTaskExecutor::scheduleWorkAt(Date_t when,
                                                                              CallbackFn&& work) {
    return _executor->scheduleWorkAt(when, std::move(work));
}

StatusWith<TaskExecutor::CallbackHandle> ShardingTaskExecutor::scheduleRemoteCommandOnAny(
    const RemoteCommandRequestOnAny& request,
    const RemoteCommandOnAnyCallbackFn& cb,
    const BatonHandle& baton) {

    // schedule the user's callback if there is not opCtx
    if (!request.opCtx) {
        return _executor->scheduleRemoteCommandOnAny(request, cb, baton);
    }

    boost::optional<RemoteCommandRequestOnAny> requestWithFixedLsid = [&] {
        boost::optional<RemoteCommandRequestOnAny> newRequest;

        if (!request.opCtx->getLogicalSessionId()) {
            return newRequest;
        }

        if (request.cmdObj.hasField("lsid")) {
            auto cmdObjLsid =
                LogicalSessionFromClient::parse("lsid"_sd, request.cmdObj["lsid"].Obj());

            if (cmdObjLsid.getUid()) {
                invariant(*cmdObjLsid.getUid() == request.opCtx->getLogicalSessionId()->getUid());
                return newRequest;
            }

            newRequest.emplace(request);
            newRequest->cmdObj = newRequest->cmdObj.removeField("lsid");
        }

        if (!newRequest) {
            newRequest.emplace(request);
        }

        BSONObjBuilder bob(std::move(newRequest->cmdObj));
        {
            BSONObjBuilder subbob(bob.subobjStart("lsid"));
            request.opCtx->getLogicalSessionId()->serialize(&subbob);
            subbob.done();
        }

        newRequest->cmdObj = bob.obj();

        return newRequest;
    }();

    std::shared_ptr<OperationTimeTracker> timeTracker = OperationTimeTracker::get(request.opCtx);

    auto clusterGLE = ClusterLastErrorInfo::get(request.opCtx->getClient());

    auto shardingCb =
        [ timeTracker, clusterGLE, cb, grid = Grid::get(request.opCtx), hosts = request.target ](
            const TaskExecutor::RemoteCommandOnAnyCallbackArgs& args) {
        ON_BLOCK_EXIT([&cb, &args]() { cb(args); });

        if (!args.response.isOK()) {
            HostAndPort target;

            if (args.response.target) {
                target = *args.response.target;
            } else {
                target = hosts.front();
            }

            auto shard = grid->shardRegistry()->getShardForHostNoReload(target);

            if (!shard) {
                LOG(1) << "Could not find shard containing host: " << target;
            }

            if (isMongers() && args.response.status == ErrorCodes::IncompatibleWithUpgradedServer) {
                severe() << "This mongers server must be upgraded. It is attempting to communicate "
                            "with "
                            "an upgraded cluster with which it is incompatible. Error: '"
                         << args.response.status.toString()
                         << "' Crashing in order to bring attention to the incompatibility, rather "
                            "than erroring endlessly.";
                fassertNoTrace(50710, false);
            }

            if (shard) {
                shard->updateReplSetMonitor(target, args.response.status);
            }

            LOG(1) << "Error processing the remote request, not updating operationTime or gLE";

            return;
        }

        invariant(args.response.target);

        auto target = *args.response.target;

        auto shard = grid->shardRegistry()->getShardForHostNoReload(target);

        if (shard) {
            shard->updateReplSetMonitor(target, getStatusFromCommandResult(args.response.data));
        }

        // Update the logical clock.
        invariant(timeTracker);
        auto operationTime = args.response.data[kOperationTimeField];
        if (!operationTime.eoo()) {
            invariant(operationTime.type() == BSONType::bsonTimestamp);
            timeTracker->updateOperationTime(LogicalTime(operationTime.timestamp()));
        }

        // Update getLastError info for the client if we're tracking it.
        if (clusterGLE) {
            auto swShardingMetadata = rpc::ShardingMetadata::readFromMetadata(args.response.data);
            if (swShardingMetadata.isOK()) {
                auto shardingMetadata = std::move(swShardingMetadata.getValue());

                auto shardConn = ConnectionString::parse(target.toString());
                if (!shardConn.isOK()) {
                    severe() << "got bad host string in saveGLEStats: " << target;
                }

                clusterGLE->addHostOpTime(shardConn.getValue(),
                                          HostOpTime(shardingMetadata.getLastOpTime(),
                                                     shardingMetadata.getLastElectionId()));
            } else if (swShardingMetadata.getStatus() != ErrorCodes::NoSuchKey) {
                warning() << "Got invalid sharding metadata "
                          << redact(swShardingMetadata.getStatus()) << " metadata object was '"
                          << redact(args.response.data) << "'";
            }
        }
    };

    return _executor->scheduleRemoteCommandOnAny(
        requestWithFixedLsid ? *requestWithFixedLsid : request, shardingCb, baton);
}

void ShardingTaskExecutor::cancel(const CallbackHandle& cbHandle) {
    _executor->cancel(cbHandle);
}

void ShardingTaskExecutor::wait(const CallbackHandle& cbHandle, Interruptible* interruptible) {
    _executor->wait(cbHandle, interruptible);
}

void ShardingTaskExecutor::appendConnectionStats(ConnectionPoolStats* stats) const {
    _executor->appendConnectionStats(stats);
}

}  // namespace executor
}  // namespace monger
