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

#include "monger/util/future.h"

#include "monger/stdx/thread.h"
#include "monger/unittest/death_test.h"
#include "monger/unittest/unittest.h"

#if !defined(__has_feature)
#define __has_feature(x) 0
#endif

namespace monger {

enum DoExecutorFuture : bool {
    // Force exemptions to say *why* they shouldn't test ExecutorFuture to ensure that if the
    // reason stops applying (eg, if we implement ExecutorFuture::tap()) we can delete the enum
    // value and recover the test coverage.
    kNoExecutorFuture_needsTap = false,
    kNoExecutorFuture_needsPromiseSetFrom = false,
    kDoExecutorFuture = true,
};

class InlineCountingExecutor final : public OutOfLineExecutor {
public:
    void schedule(Task task) noexcept override {
        // Relaxed to avoid adding synchronization where there otherwise wouldn't be. That would
        // cause a false negative from TSAN.
        tasksRun.fetch_add(1, std::memory_order_relaxed);
        task(Status::OK());
    }

    static auto make() {
        return std::make_shared<InlineCountingExecutor>();
    }

    std::atomic<int32_t> tasksRun{0};  // NOLINT
};

class RejectingExecutor final : public OutOfLineExecutor {
public:
    void schedule(Task task) noexcept override {
        task(Status(ErrorCodes::ShutdownInProgress, ""));
    }

    static auto make() {
        return std::make_shared<RejectingExecutor>();
    }
};

class DummyInterruptable final : public Interruptible {
    StatusWith<stdx::cv_status> waitForConditionOrInterruptNoAssertUntil(
        stdx::condition_variable& cv,
        stdx::unique_lock<stdx::mutex>& m,
        Date_t deadline) noexcept override {
        return Status(ErrorCodes::Interrupted, "");
    }
    Date_t getDeadline() const override {
        MONGO_UNREACHABLE;
    }
    Status checkForInterruptNoAssert() noexcept override {
        MONGO_UNREACHABLE;
    }
    IgnoreInterruptsState pushIgnoreInterrupts() override {
        MONGO_UNREACHABLE;
    }
    void popIgnoreInterrupts(IgnoreInterruptsState iis) override {
        MONGO_UNREACHABLE;
    }
    DeadlineState pushArtificialDeadline(Date_t deadline, ErrorCodes::Error error) override {
        MONGO_UNREACHABLE;
    }
    void popArtificialDeadline(DeadlineState) override {
        MONGO_UNREACHABLE;
    }
    Date_t getExpirationDateForWaitForValue(Milliseconds waitFor) override {
        MONGO_UNREACHABLE;
    }
};

template <typename T, typename Func>
void completePromise(Promise<T>* promise, Func&& func) {
    promise->emplaceValue(func());
}

template <typename Func>
void completePromise(Promise<void>* promise, Func&& func) {
    func();
    promise->emplaceValue();
}

inline void sleepIfShould() {
#if !__has_feature(thread_sanitizer)
    // TSAN and rr work better without this sleep, but it is useful for testing correctness.
    static const bool runningUnderRR = getenv("RUNNING_UNDER_RR") != nullptr;
    if (!runningUnderRR)
        sleepmillis(100);  // Try to wait until after the Future has been handled.
#endif
}

template <typename Func, typename Result = std::result_of_t<Func && ()>>
Future<Result> async(Func&& func) {
    auto pf = makePromiseFuture<Result>();

    stdx::thread([ promise = std::move(pf.promise), func = std::forward<Func>(func) ]() mutable {
        sleepIfShould();
        try {
            completePromise(&promise, func);
        } catch (const DBException& ex) {
            promise.setError(ex.toStatus());
        }
    }).detach();

    return std::move(pf.future);
}

inline Status failStatus() {
    return Status(ErrorCodes::Error(50728), "expected failure");
}

#define ASSERT_THROWS_failStatus(expr)                                          \
    [&] {                                                                       \
        ASSERT_THROWS_WITH_CHECK(expr, DBException, [](const DBException& ex) { \
            ASSERT_EQ(ex.toStatus(), failStatus());                             \
        });                                                                     \
    }()

// Tests a Future completed by completionExpr using testFunc. The Future will be completed in
// various ways to maximize test coverage.
template <DoExecutorFuture doExecutorFuture = kDoExecutorFuture,
          typename CompletionFunc,
          typename TestFunc,
          typename = std::enable_if_t<!std::is_void<std::result_of_t<CompletionFunc()>>::value>>
void FUTURE_SUCCESS_TEST(const CompletionFunc& completion, const TestFunc& test) {
    using CompletionType = decltype(completion());
    {  // immediate future
        test(Future<CompletionType>::makeReady(completion()));
    }
    {  // ready future from promise
        auto pf = makePromiseFuture<CompletionType>();
        pf.promise.emplaceValue(completion());
        test(std::move(pf.future));
    }

    {  // async future
        test(async([&] { return completion(); }));
    }

    IF_CONSTEXPR(doExecutorFuture) {  // immediate executor future
        auto exec = InlineCountingExecutor::make();
        test(Future<CompletionType>::makeReady(completion()).thenRunOn(exec));
    }
}

template <DoExecutorFuture doExecutorFuture = kDoExecutorFuture,
          typename CompletionFunc,
          typename TestFunc,
          typename = std::enable_if_t<std::is_void<std::result_of_t<CompletionFunc()>>::value>,
          typename = void>
void FUTURE_SUCCESS_TEST(const CompletionFunc& completion, const TestFunc& test) {
    using CompletionType = decltype(completion());
    {  // immediate future
        completion();
        test(Future<CompletionType>::makeReady());
    }
    {  // ready future from promise
        auto pf = makePromiseFuture<CompletionType>();
        completion();
        pf.promise.emplaceValue();
        test(std::move(pf.future));
    }

    {  // async future
        test(async([&] { return completion(); }));
    }

    IF_CONSTEXPR(doExecutorFuture) {  // immediate executor future
        completion();
        auto exec = InlineCountingExecutor::make();
        test(Future<CompletionType>::makeReady().thenRunOn(exec));
    }
}

template <typename CompletionType,
          DoExecutorFuture doExecutorFuture = kDoExecutorFuture,
          typename TestFunc>
void FUTURE_FAIL_TEST(const TestFunc& test) {
    {  // immediate future
        test(Future<CompletionType>::makeReady(failStatus()));
    }
    {  // ready future from promise
        auto pf = makePromiseFuture<CompletionType>();
        pf.promise.setError(failStatus());
        test(std::move(pf.future));
    }

    {  // async future
        test(async([&]() -> CompletionType {
            uassertStatusOK(failStatus());
            MONGO_UNREACHABLE;
        }));
    }
    IF_CONSTEXPR(doExecutorFuture) {  // immediate executor future
        auto exec = InlineCountingExecutor::make();
        test(Future<CompletionType>::makeReady(failStatus()).thenRunOn(exec));
    }
}
}  // namespace monger
