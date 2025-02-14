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

#include <functional>

#include "monger/db/logical_session_id.h"
#include "monger/db/sessions_collection.h"
#include "monger/stdx/mutex.h"
#include "monger/stdx/unordered_map.h"

namespace monger {

/**
 * To allow us to move a MockSessionCollection into the session cache while
 * maintaining a hold on it from within our unit tests, the MockSessionCollection
 * will have an internal pointer to a MockSessionsCollectionImpl object that the
 * test creates and controls.
 *
 * This class can operate in two modes:
 *
 * - if no custom hooks are set, then the test caller may add() and remove() items
 *   from the _sessions map, and this class will simply perform the required
 *   operations against that set.
 *
 * - if custom hooks are set, then the hooks will be run instead of the provided
 *   defaults, and the internal _sessions map will NOT be updated.
 */
class MockSessionsCollectionImpl {
public:
    using SessionMap =
        stdx::unordered_map<LogicalSessionId, LogicalSessionRecord, LogicalSessionIdHash>;

    MockSessionsCollectionImpl();

    using RefreshHook = std::function<Status(const LogicalSessionRecordSet&)>;
    using RemoveHook = std::function<Status(const LogicalSessionIdSet&)>;

    // Set custom hooks to override default behavior
    void setRefreshHook(RefreshHook hook);
    void setRemoveHook(RemoveHook hook);

    // Reset all hooks to their defaults
    void clearHooks();

    // Forwarding methods from the MockSessionsCollection
    Status refreshSessions(const LogicalSessionRecordSet& sessions);
    Status removeRecords(const LogicalSessionIdSet& sessions);

    // Test-side methods that operate on the _sessions map
    void add(LogicalSessionRecord record);
    void remove(LogicalSessionId lsid);
    bool has(LogicalSessionId lsid);
    void clearSessions();
    const SessionMap& sessions() const;

    StatusWith<LogicalSessionIdSet> findRemovedSessions(OperationContext* opCtx,
                                                        const LogicalSessionIdSet& sessions);

private:
    // Default implementations, may be overridden with custom hooks.
    Status _refreshSessions(const LogicalSessionRecordSet& sessions);
    Status _removeRecords(const LogicalSessionIdSet& sessions);

    stdx::mutex _mutex;
    SessionMap _sessions;

    RefreshHook _refresh;
    RemoveHook _remove;
};

/**
 * To allow us to move this into the session cache while maintaining a hold
 * on it from the test side, the MockSessionCollection will have an internal pointer
 * to an impl that we maintain access to.
 */
class MockSessionsCollection : public SessionsCollection {
public:
    explicit MockSessionsCollection(std::shared_ptr<MockSessionsCollectionImpl> impl)
        : _impl(std::move(impl)) {}

    Status setupSessionsCollection(OperationContext* opCtx) override {
        return Status::OK();
    }

    Status checkSessionsCollectionExists(OperationContext* opCtx) override {
        return Status::OK();
    }

    Status refreshSessions(OperationContext* opCtx,
                           const LogicalSessionRecordSet& sessions) override {
        return _impl->refreshSessions(sessions);
    }

    Status removeRecords(OperationContext* opCtx, const LogicalSessionIdSet& sessions) override {
        return _impl->removeRecords(sessions);
    }

    StatusWith<LogicalSessionIdSet> findRemovedSessions(
        OperationContext* opCtx, const LogicalSessionIdSet& sessions) override {
        return _impl->findRemovedSessions(opCtx, sessions);
    }

private:
    std::shared_ptr<MockSessionsCollectionImpl> _impl;
};

}  // namespace monger
