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

#include "monger/db/repl/abstract_oplog_fetcher.h"
#include "monger/executor/thread_pool_task_executor_test_fixture.h"
#include "monger/unittest/unittest.h"

namespace monger {
namespace repl {

/**
 * This class represents the state at shutdown of an abstract oplog fetcher.
 */
class ShutdownState {
    ShutdownState(const ShutdownState&) = delete;
    ShutdownState& operator=(const ShutdownState&) = delete;

public:
    ShutdownState();

    /**
     * Returns the status at shutdown.
     */
    Status getStatus() const;

    /**
     * Use this for oplog fetcher shutdown callback.
     */
    void operator()(const Status& status);

private:
    Status _status = executor::TaskExecutorTest::getDetectableErrorStatus();
};

/**
 * This class contains many of the functions used by all oplog fetcher test suites.
 */
class AbstractOplogFetcherTest : public executor::ThreadPoolExecutorTest {
public:
    /**
     * Static functions for creating noop oplog entries.
     */
    static BSONObj makeNoopOplogEntry(OpTime opTime);
    static BSONObj makeNoopOplogEntry(Seconds seconds);

    /**
     * A static function for creating the response to a cursor. If it's the last batch, the
     * cursorId provided should be 0.
     */
    static BSONObj makeCursorResponse(
        CursorId cursorId,
        Fetcher::Documents oplogEntries,
        bool isFirstBatch = true,
        const NamespaceString& nss = NamespaceString("local.oplog.rs"));

protected:
    void setUp() override;

    /**
     * Schedules network response and instructs network interface to process response.
     * Returns remote command request in network request.
     */
    executor::RemoteCommandRequest processNetworkResponse(
        executor::RemoteCommandResponse response, bool expectReadyRequestsAfterProcessing = false);
    executor::RemoteCommandRequest processNetworkResponse(
        BSONObj obj, bool expectReadyRequestsAfterProcessing = false);

    // The last OpTime fetched by the oplog fetcher.
    OpTime lastFetched;
    Date_t lastFetchedWall;
};
}  // namespace repl
}  // namespace mango
