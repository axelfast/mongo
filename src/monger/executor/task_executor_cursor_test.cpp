/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kNetwork

#include "monger/platform/basic.h"

#include "monger/executor/task_executor_cursor.h"
#include "monger/executor/thread_pool_task_executor_test_fixture.h"
#include "monger/unittest/bson_test_util.h"
#include "monger/unittest/unittest.h"

namespace monger {
namespace executor {
namespace {

/**
 * Fixture for the task executor cursor tests which offers some convenience methods to help with
 * scheduling responses
 */
class TaskExecutorCursorFixture : public ThreadPoolExecutorTest {
public:
    void setUp() override {
        ThreadPoolExecutorTest::setUp();

        client = serviceCtx->makeClient("TaskExecutorCursorTest");
        opCtx = client->makeOperationContext();

        launchExecutorThread();
    }

    void tearDown() override {
        opCtx.reset();
        client.reset();

        ThreadPoolExecutorTest::tearDown();
    }

    BSONObj scheduleSuccessfulCursorResponse(StringData fieldName,
                                             size_t start,
                                             size_t end,
                                             size_t cursorId) {
        NetworkInterfaceMock::InNetworkGuard ing(getNet());

        BSONObjBuilder bob;
        {
            BSONObjBuilder cursor(bob.subobjStart("cursor"));
            {
                BSONArrayBuilder batch(cursor.subarrayStart(fieldName));

                for (size_t i = start; i <= end; ++i) {
                    BSONObjBuilder doc(batch.subobjStart());
                    doc.append("x", int(i));
                }
            }
            cursor.append("id", (long long)(cursorId));
            cursor.append("ns", "test.test");
        }
        bob.append("ok", int(1));

        ASSERT(getNet()->hasReadyRequests());
        auto rcr = getNet()->scheduleSuccessfulResponse(bob.obj());
        getNet()->runReadyNetworkOperations();

        return rcr.cmdObj.getOwned();
    }

    BSONObj scheduleSuccessfulKillCursorResponse(size_t cursorId) {
        NetworkInterfaceMock::InNetworkGuard ing(getNet());

        ASSERT(getNet()->hasReadyRequests());
        auto rcr = getNet()->scheduleSuccessfulResponse(BSON(
            "cursorsKilled" << BSON_ARRAY((long long)(cursorId)) << "cursorsNotFound" << BSONArray()
                            << "cursorsAlive"
                            << BSONArray()
                            << "cursorsUnknown"
                            << BSONArray()
                            << "ok"
                            << 1));
        getNet()->runReadyNetworkOperations();

        return rcr.cmdObj.getOwned();
    }

    bool hasReadyRequests() {
        NetworkInterfaceMock::InNetworkGuard ing(getNet());
        return getNet()->hasReadyRequests();
    }

    ServiceContext::UniqueServiceContext serviceCtx = ServiceContext::make();
    ServiceContext::UniqueClient client;
    ServiceContext::UniqueOperationContext opCtx;
};

/**
 * Ensure we work for a single simple batch
 */
TEST_F(TaskExecutorCursorFixture, SingleBatchWorks) {
    auto findCmd = BSON("find"
                        << "test"
                        << "batchSize"
                        << 2);

    RemoteCommandRequest rcr(HostAndPort("localhost"), "test", findCmd, opCtx.get());

    TaskExecutorCursor tec(&getExecutor(), rcr);

    ASSERT_BSONOBJ_EQ(findCmd, scheduleSuccessfulCursorResponse("firstBatch", 1, 2, 0));

    ASSERT_EQUALS(tec.getNext(opCtx.get()).get()["x"].Int(), 1);

    ASSERT_FALSE(hasReadyRequests());

    ASSERT_EQUALS(tec.getNext(opCtx.get()).get()["x"].Int(), 2);

    ASSERT_FALSE(tec.getNext(opCtx.get()));
}

/**
 * Ensure we work if find fails (and that we receive the error code it failed with)
 */
TEST_F(TaskExecutorCursorFixture, FailureInFind) {
    RemoteCommandRequest rcr(HostAndPort("localhost"),
                             "test",
                             BSON("find"
                                  << "test"
                                  << "batchSize"
                                  << 2),
                             opCtx.get());

    TaskExecutorCursor tec(&getExecutor(), rcr);

    {
        NetworkInterfaceMock::InNetworkGuard ing(getNet());

        ASSERT(getNet()->hasReadyRequests());
        getNet()->scheduleErrorResponse(Status(ErrorCodes::BadValue, "an error"));
        getNet()->runReadyNetworkOperations();
    }

    ASSERT_THROWS_CODE(tec.getNext(opCtx.get()), DBException, ErrorCodes::BadValue);
}

/**
 * Ensure early termination of the cursor calls killCursor (if we know about the cursor id)
 */
TEST_F(TaskExecutorCursorFixture, EarlyReturnKillsCursor) {
    RemoteCommandRequest rcr(HostAndPort("localhost"),
                             "test",
                             BSON("find"
                                  << "test"
                                  << "batchSize"
                                  << 2),
                             opCtx.get());

    {
        TaskExecutorCursor tec(&getExecutor(), rcr);

        scheduleSuccessfulCursorResponse("firstBatch", 1, 2, 1);

        ASSERT(tec.getNext(opCtx.get()));
    }

    ASSERT_BSONOBJ_EQ(BSON("killCursors"
                           << "test"
                           << "cursors"
                           << BSON_ARRAY(1)),
                      scheduleSuccessfulKillCursorResponse(1));
}

/**
 * Ensure multiple batches works correctly
 */
TEST_F(TaskExecutorCursorFixture, MultipleBatchesWorks) {
    RemoteCommandRequest rcr(HostAndPort("localhost"),
                             "test",
                             BSON("find"
                                  << "test"
                                  << "batchSize"
                                  << 2),
                             opCtx.get());

    TaskExecutorCursor tec(&getExecutor(), rcr, [] {
        TaskExecutorCursor::Options opts;
        opts.batchSize = 3;
        return opts;
    }());

    scheduleSuccessfulCursorResponse("firstBatch", 1, 2, 1);

    ASSERT_EQUALS(tec.getNext(opCtx.get()).get()["x"].Int(), 1);

    ASSERT(hasReadyRequests());

    ASSERT_EQUALS(tec.getNext(opCtx.get()).get()["x"].Int(), 2);

    // If we try to getNext() at this point, we are interruptible and can timeout
    ASSERT_THROWS_CODE(opCtx->runWithDeadline(Date_t::now() + Milliseconds(100),
                                              ErrorCodes::ExceededTimeLimit,
                                              [&] { tec.getNext(opCtx.get()); }),
                       DBException,
                       ErrorCodes::ExceededTimeLimit);

    // We can pick up after that interruption though
    ASSERT_BSONOBJ_EQ(BSON("getMore" << (long long)(1) << "collection"
                                     << "test"
                                     << "batchSize"
                                     << 3),
                      scheduleSuccessfulCursorResponse("nextBatch", 3, 5, 1));

    ASSERT_EQUALS(tec.getNext(opCtx.get()).get()["x"].Int(), 3);
    ASSERT_EQUALS(tec.getNext(opCtx.get()).get()["x"].Int(), 4);
    ASSERT_EQUALS(tec.getNext(opCtx.get()).get()["x"].Int(), 5);

    scheduleSuccessfulCursorResponse("nextBatch", 6, 6, 0);

    // We don't issue extra getmores after returning a 0 cursor id
    ASSERT_FALSE(hasReadyRequests());

    ASSERT_EQUALS(tec.getNext(opCtx.get()).get()["x"].Int(), 6);

    ASSERT_FALSE(tec.getNext(opCtx.get()));
}

/**
 * Ensure lsid is passed in all stages of querying
 */
TEST_F(TaskExecutorCursorFixture, LsidIsPassed) {
    auto lsid = makeLogicalSessionIdForTest();
    opCtx->setLogicalSessionId(lsid);

    auto findCmd = BSON("find"
                        << "test"
                        << "batchSize"
                        << 1);

    RemoteCommandRequest rcr(HostAndPort("localhost"), "test", findCmd, opCtx.get());

    boost::optional<TaskExecutorCursor> tec;
    tec.emplace(&getExecutor(), rcr, []() {
        TaskExecutorCursor::Options opts;
        opts.batchSize = 1;
        return opts;
    }());

    // lsid in the first batch
    ASSERT_BSONOBJ_EQ(BSON("find"
                           << "test"
                           << "batchSize"
                           << 1
                           << "lsid"
                           << lsid.toBSON()),
                      scheduleSuccessfulCursorResponse("firstBatch", 1, 1, 1));

    ASSERT_EQUALS(tec->getNext(opCtx.get()).get()["x"].Int(), 1);

    // lsid in the getmore
    ASSERT_BSONOBJ_EQ(BSON("getMore" << (long long)(1) << "collection"
                                     << "test"
                                     << "batchSize"
                                     << 1
                                     << "lsid"
                                     << lsid.toBSON()),
                      scheduleSuccessfulCursorResponse("nextBatch", 2, 2, 1));

    tec.reset();

    // lsid in the killcursor
    ASSERT_BSONOBJ_EQ(BSON("killCursors"
                           << "test"
                           << "cursors"
                           << BSON_ARRAY(1)
                           << "lsid"
                           << lsid.toBSON()),
                      scheduleSuccessfulKillCursorResponse(1));

    ASSERT_FALSE(hasReadyRequests());
}

}  // namespace
}  // namespace executor
}  // namespace monger
