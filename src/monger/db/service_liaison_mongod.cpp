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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kControl

#include "monger/platform/basic.h"

#include "monger/db/service_liaison_mongerd.h"

#include "monger/db/client.h"
#include "monger/db/cursor_manager.h"
#include "monger/db/operation_context.h"
#include "monger/db/service_context.h"
#include "monger/stdx/mutex.h"
#include "monger/util/clock_source.h"
#include "monger/util/log.h"

namespace monger {

LogicalSessionIdSet ServiceLiaisonMongerd::getActiveOpSessions() const {
    LogicalSessionIdSet activeSessions;

    invariant(hasGlobalServiceContext());

    // Walk through the service context and append lsids for all currently-running ops.
    for (ServiceContext::LockedClientsCursor cursor(getGlobalServiceContext());
         Client* client = cursor.next();) {

        stdx::lock_guard<Client> lk(*client);
        auto clientOpCtx = client->getOperationContext();

        // Ignore clients without currently-running operations
        if (!clientOpCtx)
            continue;

        // Append this op ctx's session to our list, if it has one
        auto lsid = clientOpCtx->getLogicalSessionId();
        if (lsid) {
            activeSessions.insert(*lsid);
        }
    }
    return activeSessions;
}

LogicalSessionIdSet ServiceLiaisonMongerd::getOpenCursorSessions(OperationContext* opCtx) const {
    LogicalSessionIdSet cursorSessions;
    CursorManager::get(opCtx)->appendActiveSessions(&cursorSessions);
    return cursorSessions;
}

void ServiceLiaisonMongerd::scheduleJob(PeriodicRunner::PeriodicJob job) {
    invariant(hasGlobalServiceContext());
    auto jobAnchor = getGlobalServiceContext()->getPeriodicRunner()->makeJob(std::move(job));
    jobAnchor.start();

    {
        stdx::lock_guard lk(_mutex);
        _jobs.push_back(std::move(jobAnchor));
    }
}

void ServiceLiaisonMongerd::join() {
    auto jobs = [&] {
        stdx::lock_guard lk(_mutex);
        return std::exchange(_jobs, {});
    }();
}

Date_t ServiceLiaisonMongerd::now() const {
    invariant(hasGlobalServiceContext());
    return getGlobalServiceContext()->getFastClockSource()->now();
}

ServiceContext* ServiceLiaisonMongerd::_context() {
    return getGlobalServiceContext();
}

std::pair<Status, int> ServiceLiaisonMongerd::killCursorsWithMatchingSessions(
    OperationContext* opCtx, const SessionKiller::Matcher& matcher) {
    return CursorManager::get(opCtx)->killCursorsWithMatchingSessions(opCtx, matcher);
}

}  // namespace monger
