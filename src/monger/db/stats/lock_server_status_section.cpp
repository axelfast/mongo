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

#include <valarray>

#include "monger/db/client.h"
#include "monger/db/commands/server_status.h"
#include "monger/db/concurrency/lock_stats.h"
#include "monger/db/jsobj.h"
#include "monger/db/operation_context.h"

namespace monger {
namespace {

class GlobalLockServerStatusSection : public ServerStatusSection {
public:
    GlobalLockServerStatusSection() : ServerStatusSection("globalLock") {
        _started = curTimeMillis64();
    }

    bool includeByDefault() const override {
        return true;
    }

    BSONObj generateSection(OperationContext* opCtx,
                            const BSONElement& configElement) const override {
        std::valarray<int> clientStatusCounts(5);

        // This returns the blocked lock states
        for (ServiceContext::LockedClientsCursor cursor(opCtx->getClient()->getServiceContext());
             Client* client = cursor.next();) {
            invariant(client);
            stdx::unique_lock<Client> uniqueLock(*client);

            const OperationContext* clientOpCtx = client->getOperationContext();
            auto state =
                clientOpCtx ? clientOpCtx->lockState()->getClientState() : Locker::kInactive;
            invariant(state < sizeof(clientStatusCounts));
            clientStatusCounts[state]++;
        }

        // Construct the actual return value out of the mutex
        BSONObjBuilder ret;

        ret.append("totalTime", (long long)(1000 * (curTimeMillis64() - _started)));

        {
            BSONObjBuilder currentQueueBuilder(ret.subobjStart("currentQueue"));

            currentQueueBuilder.append("total",
                                       clientStatusCounts[Locker::kQueuedReader] +
                                           clientStatusCounts[Locker::kQueuedWriter]);
            currentQueueBuilder.append("readers", clientStatusCounts[Locker::kQueuedReader]);
            currentQueueBuilder.append("writers", clientStatusCounts[Locker::kQueuedWriter]);
            currentQueueBuilder.done();
        }

        {
            BSONObjBuilder activeClientsBuilder(ret.subobjStart("activeClients"));

            activeClientsBuilder.append("total",
                                        clientStatusCounts[Locker::kActiveReader] +
                                            clientStatusCounts[Locker::kActiveWriter]);
            activeClientsBuilder.append("readers", clientStatusCounts[Locker::kActiveReader]);
            activeClientsBuilder.append("writers", clientStatusCounts[Locker::kActiveWriter]);
            activeClientsBuilder.done();
        }

        ret.done();

        return ret.obj();
    }

private:
    unsigned long long _started;

} globalLockServerStatusSection;


class LockStatsServerStatusSection : public ServerStatusSection {
public:
    LockStatsServerStatusSection() : ServerStatusSection("locks") {}

    bool includeByDefault() const override {
        return true;
    }

    BSONObj generateSection(OperationContext* opCtx,
                            const BSONElement& configElement) const override {
        BSONObjBuilder ret;

        SingleThreadedLockStats stats;
        reportGlobalLockingStats(&stats);

        stats.report(&ret);

        return ret.obj();
    }

} lockStatsServerStatusSection;

}  // namespace
}  // namespace monger
