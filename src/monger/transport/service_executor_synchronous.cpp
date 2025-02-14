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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kExecutor;

#include "monger/platform/basic.h"

#include "monger/transport/service_executor_synchronous.h"

#include "monger/stdx/thread.h"
#include "monger/transport/service_entry_point_utils.h"
#include "monger/transport/service_executor_gen.h"
#include "monger/transport/service_executor_task_names.h"
#include "monger/util/log.h"
#include "monger/util/processinfo.h"

namespace monger {
namespace transport {
namespace {
constexpr auto kThreadsRunning = "threadsRunning"_sd;
constexpr auto kExecutorLabel = "executor"_sd;
constexpr auto kExecutorName = "passthrough"_sd;
}  // namespace

thread_local std::deque<ServiceExecutor::Task> ServiceExecutorSynchronous::_localWorkQueue = {};
thread_local int ServiceExecutorSynchronous::_localRecursionDepth = 0;
thread_local int64_t ServiceExecutorSynchronous::_localThreadIdleCounter = 0;

ServiceExecutorSynchronous::ServiceExecutorSynchronous(ServiceContext* ctx) {}

Status ServiceExecutorSynchronous::start() {
    _numHardwareCores = static_cast<size_t>(ProcessInfo::getNumAvailableCores());

    _stillRunning.store(true);

    return Status::OK();
}

Status ServiceExecutorSynchronous::shutdown(Milliseconds timeout) {
    LOG(3) << "Shutting down passthrough executor";

    _stillRunning.store(false);

    stdx::unique_lock<stdx::mutex> lock(_shutdownMutex);
    bool result = _shutdownCondition.wait_for(lock, timeout.toSystemDuration(), [this]() {
        return _numRunningWorkerThreads.load() == 0;
    });

    return result
        ? Status::OK()
        : Status(ErrorCodes::Error::ExceededTimeLimit,
                 "passthrough executor couldn't shutdown all worker threads within time limit.");
}

Status ServiceExecutorSynchronous::schedule(Task task,
                                            ScheduleFlags flags,
                                            ServiceExecutorTaskName taskName) {
    if (!_stillRunning.load()) {
        return Status{ErrorCodes::ShutdownInProgress, "Executor is not running"};
    }

    if (!_localWorkQueue.empty()) {
        /*
         * In perf testing we found that yielding after running a each request produced
         * at 5% performance boost in microbenchmarks if the number of worker threads
         * was greater than the number of available cores.
         */
        if (flags & ScheduleFlags::kMayYieldBeforeSchedule) {
            if (_numRunningWorkerThreads.loadRelaxed() > _numHardwareCores) {
                stdx::this_thread::yield();
            }
        }

        // Execute task directly (recurse) if allowed by the caller as it produced better
        // performance in testing. Try to limit the amount of recursion so we don't blow up the
        // stack, even though this shouldn't happen with this executor that uses blocking network
        // I/O.
        if ((flags & ScheduleFlags::kMayRecurse) &&
            (_localRecursionDepth < synchronousServiceExecutorRecursionLimit.loadRelaxed())) {
            ++_localRecursionDepth;
            task();
        } else {
            _localWorkQueue.emplace_back(std::move(task));
        }
        return Status::OK();
    }

    // First call to schedule() for this connection, spawn a worker thread that will push jobs
    // into the thread local job queue.
    LOG(3) << "Starting new executor thread in passthrough mode";

    Status status = launchServiceWorkerThread([ this, task = std::move(task) ] {
        _numRunningWorkerThreads.addAndFetch(1);

        _localWorkQueue.emplace_back(std::move(task));
        while (!_localWorkQueue.empty() && _stillRunning.loadRelaxed()) {
            _localRecursionDepth = 1;
            _localWorkQueue.front()();
            _localWorkQueue.pop_front();
        }

        if (_numRunningWorkerThreads.subtractAndFetch(1) == 0) {
            _shutdownCondition.notify_all();
        }
    });

    return status;
}

void ServiceExecutorSynchronous::appendStats(BSONObjBuilder* bob) const {
    *bob << kExecutorLabel << kExecutorName << kThreadsRunning
         << static_cast<int>(_numRunningWorkerThreads.loadRelaxed());
}

}  // namespace transport
}  // namespace monger
