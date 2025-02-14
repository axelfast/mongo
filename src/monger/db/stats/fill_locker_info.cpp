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

#include "monger/db/stats/fill_locker_info.h"

#include <algorithm>

#include "monger/db/concurrency/locker.h"
#include "monger/db/jsobj.h"

namespace monger {

void fillLockerInfo(const Locker::LockerInfo& lockerInfo, BSONObjBuilder& infoBuilder) {
    // "locks" section
    BSONObjBuilder locks(infoBuilder.subobjStart("locks"));
    const size_t locksSize = lockerInfo.locks.size();

    // Only add the last lock of each type, and use the largest mode encountered
    LockMode modeForType[ResourceTypesCount] = {};  // default initialize to zero (min value)
    for (size_t i = 0; i < locksSize; i++) {
        const Locker::OneLock& lock = lockerInfo.locks[i];
        const ResourceType lockType = lock.resourceId.getType();
        const LockMode lockMode = std::max(lock.mode, modeForType[lockType]);

        // Check that lockerInfo is sorted on resource type
        invariant(i == 0 || lockType >= lockerInfo.locks[i - 1].resourceId.getType());

        if (lock.resourceId == resourceIdLocalDB) {
            locks.append("local", legacyModeName(lock.mode));
            continue;
        }

        modeForType[lockType] = lockMode;

        if (i + 1 < locksSize && lockerInfo.locks[i + 1].resourceId.getType() == lockType) {
            continue;  // skip this lock as it is not the last one of its type
        } else {
            locks.append(resourceTypeName(lockType), legacyModeName(lockMode));
        }
    }
    locks.done();

    // "waitingForLock" section
    infoBuilder.append("waitingForLock", lockerInfo.waitingResource.isValid());

    // "lockStats" section
    {
        BSONObjBuilder lockStats(infoBuilder.subobjStart("lockStats"));
        lockerInfo.stats.report(&lockStats);
        lockStats.done();
    }
}

}  // namespace monger
