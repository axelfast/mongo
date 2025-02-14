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

#include "monger/base/checked_cast.h"
#include "monger/base/init.h"
#include "monger/base/status.h"
#include "monger/executor/network_interface.h"
#include "monger/executor/network_interface_mock.h"
#include "monger/executor/task_executor_test_common.h"
#include "monger/executor/task_executor_test_fixture.h"
#include "monger/executor/thread_pool_mock.h"
#include "monger/executor/thread_pool_task_executor.h"
#include "monger/executor/thread_pool_task_executor_test_fixture.h"
#include "monger/unittest/barrier.h"
#include "monger/unittest/unittest.h"
#include "monger/util/fail_point_service.h"

namespace monger {
namespace executor {
namespace {

MONGO_INITIALIZER(ThreadPoolExecutorCommonTests)(InitializerContext*) {
    addTestsForExecutor("ThreadPoolExecutorCommon", [](std::unique_ptr<NetworkInterfaceMock> net) {
        return makeThreadPoolTestExecutor(std::move(net));
    });
    return Status::OK();
}

TEST_F(ThreadPoolExecutorTest, TimelyCancelationOfScheduleWorkAt) {
    auto net = getNet();
    auto& executor = getExecutor();
    launchExecutorThread();
    auto status1 = getDetectableErrorStatus();
    const auto now = net->now();
    const auto cb1 = unittest::assertGet(executor.scheduleWorkAt(
        now + Milliseconds(5000),
        [&](const TaskExecutor::CallbackArgs& cbData) { status1 = cbData.status; }));

    const auto startTime = net->now();
    net->enterNetwork();
    net->runUntil(startTime + Milliseconds(200));
    net->exitNetwork();
    executor.cancel(cb1);
    executor.wait(cb1);
    ASSERT_EQUALS(ErrorCodes::CallbackCanceled, status1);
    ASSERT_EQUALS(startTime + Milliseconds(200), net->now());
}

TEST_F(ThreadPoolExecutorTest, Schedule) {
    auto& executor = getExecutor();
    launchExecutorThread();
    auto status1 = getDetectableErrorStatus();
    unittest::Barrier barrier{2};
    executor.schedule([&](Status status) {
        status1 = status;
        barrier.countDownAndWait();
    });
    barrier.countDownAndWait();
    ASSERT_OK(status1);
}

TEST_F(ThreadPoolExecutorTest, ScheduleAfterShutdown) {
    auto& executor = getExecutor();
    auto status1 = getDetectableErrorStatus();
    executor.shutdown();
    executor.schedule([&](Status status) { status1 = status; });
    ASSERT_EQUALS(ErrorCodes::ShutdownInProgress, status1);
}

TEST_F(ThreadPoolExecutorTest, OnEvent) {
    auto& executor = getExecutor();
    launchExecutorThread();
    auto status1 = getDetectableErrorStatus();
    auto event = executor.makeEvent().getValue();
    unittest::Barrier barrier{2};
    TaskExecutor::CallbackFn cb = [&](const TaskExecutor::CallbackArgs& args) {
        status1 = args.status;
        barrier.countDownAndWait();
    };
    ASSERT_OK(executor.onEvent(event, std::move(cb)).getStatus());
    // Callback was moved from.
    ASSERT(!cb);
    executor.signalEvent(event);
    barrier.countDownAndWait();
    ASSERT_OK(status1);
}

TEST_F(ThreadPoolExecutorTest, OnEventAfterShutdown) {
    auto& executor = getExecutor();
    auto status1 = getDetectableErrorStatus();
    auto event = executor.makeEvent().getValue();
    TaskExecutor::CallbackFn cb = [&](const TaskExecutor::CallbackArgs& args) {
        status1 = args.status;
    };
    executor.shutdown();
    ASSERT_EQUALS(ErrorCodes::ShutdownInProgress,
                  executor.onEvent(event, std::move(cb)).getStatus());

    // Callback was not moved from, it is still valid and we can call it to set status1.
    ASSERT(static_cast<bool>(cb));
    TaskExecutor::CallbackArgs args(&executor, {}, Status::OK());
    cb(args);
    ASSERT_OK(status1);
}

bool sharedCallbackStateDestroyed = false;
class SharedCallbackState {
    SharedCallbackState(const SharedCallbackState&) = delete;
    SharedCallbackState& operator=(const SharedCallbackState&) = delete;

public:
    SharedCallbackState() {}
    ~SharedCallbackState() {
        sharedCallbackStateDestroyed = true;
    }
};

TEST_F(ThreadPoolExecutorTest,
       ExecutorResetsCallbackFunctionInCallbackStateUponReturnFromCallbackFunction) {
    auto net = getNet();
    auto& executor = getExecutor();
    launchExecutorThread();

    auto sharedCallbackData = std::make_shared<SharedCallbackState>();
    auto callbackInvoked = false;

    const auto when = net->now() + Milliseconds(5000);
    const auto cb1 = unittest::assertGet(executor.scheduleWorkAt(
        when, [&callbackInvoked, sharedCallbackData](const executor::TaskExecutor::CallbackArgs&) {
            callbackInvoked = true;
        }));

    sharedCallbackData.reset();
    ASSERT_FALSE(sharedCallbackStateDestroyed);

    net->enterNetwork();
    ASSERT_EQUALS(when, net->runUntil(when));
    net->exitNetwork();

    executor.wait(cb1);

    // Task executor should reset CallbackState::callback after running callback function.
    // This ensures that we release resources associated with 'CallbackState::callback' without
    // having to destroy every outstanding callback handle (which contains a shared pointer
    // to ThreadPoolTaskExecutor::CallbackState).
    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(sharedCallbackStateDestroyed);
}

thread_local bool amRunningRecursively = false;

TEST_F(ThreadPoolExecutorTest, ShutdownAndScheduleWorkRaceDoesNotCrash) {
    // This test works by scheduling a work item in the ThreadPoolTaskExecutor that blocks waiting
    // to be signaled by this thread. Once that work item is scheduled, this thread enables a
    // FailPoint that causes future calls of ThreadPoolTaskExecutor::scheduleIntoPool_inlock to spin
    // until it is shutdown, forcing a race between the caller of schedule and the caller of
    // shutdown.  The failpoint ensures that this thread spins until the task executor thread begins
    // spinning on the state transitioning to shutting down, then this thread tells the task
    // executor to shut down. Once the executor shuts down, the previously blocked
    // scheduleIntoPool_inlock unblocks, and discovers the executor to be shut down. The correct
    // behavior is for all scheduled callbacks to execute, and for this last callback at least to
    // execute with CallbackCanceled as its status.

    unittest::Barrier barrier{2};
    auto status1 = getDetectableErrorStatus();
    StatusWith<TaskExecutor::CallbackHandle> cb2 = getDetectableErrorStatus();
    auto status2 = getDetectableErrorStatus();
    auto& executor = getExecutor();
    launchExecutorThread();

    ASSERT_OK(executor
                  .scheduleWork([&](const TaskExecutor::CallbackArgs& cbData) {
                      status1 = cbData.status;
                      if (!status1.isOK())
                          return;
                      barrier.countDownAndWait();

                      amRunningRecursively = true;
                      cb2 = cbData.executor->scheduleWork(
                          [&status2](const TaskExecutor::CallbackArgs& cbData) {
                              ASSERT_FALSE(amRunningRecursively);
                              status2 = cbData.status;
                          });
                      amRunningRecursively = false;
                  })
                  .getStatus());

    auto fpTPTE1 = getGlobalFailPointRegistry()->getFailPoint(
        "scheduleIntoPoolSpinsUntilThreadPoolTaskExecutorShutsDown");
    fpTPTE1->setMode(FailPoint::alwaysOn);
    barrier.countDownAndWait();
    MONGO_FAIL_POINT_PAUSE_WHILE_SET((*fpTPTE1));
    executor.shutdown();
    executor.join();
    ASSERT_OK(status1);
    ASSERT_EQUALS(ErrorCodes::CallbackCanceled, status2);
}

TEST_F(ThreadPoolExecutorTest, ShutdownAndScheduleRaceDoesNotCrash) {
    // Same as above, with schedule() instead of scheduleWork().
    unittest::Barrier barrier{2};
    auto status1 = getDetectableErrorStatus();
    auto status2 = getDetectableErrorStatus();
    auto& executor = getExecutor();
    launchExecutorThread();

    executor.schedule([&](Status status) {
        status1 = status;
        if (!status1.isOK())
            return;
        barrier.countDownAndWait();
        amRunningRecursively = true;
        executor.schedule([&status2](Status status) {
            ASSERT_FALSE(amRunningRecursively);
            status2 = status;
        });
        amRunningRecursively = false;
    });

    auto fpTPTE1 = getGlobalFailPointRegistry()->getFailPoint(
        "scheduleIntoPoolSpinsUntilThreadPoolTaskExecutorShutsDown");
    fpTPTE1->setMode(FailPoint::alwaysOn);
    barrier.countDownAndWait();
    MONGO_FAIL_POINT_PAUSE_WHILE_SET((*fpTPTE1));
    executor.shutdown();
    executor.join();
    ASSERT_OK(status1);
    ASSERT_EQUALS(ErrorCodes::CallbackCanceled, status2);
}

}  // namespace
}  // namespace executor
}  // namespace monger
