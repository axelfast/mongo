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

#include "monger/idl/mutable_observer_registry.h"
#include "monger/platform/atomic_proxy.h"
#include "monger/platform/atomic_word.h"

namespace monger {

/**
 * These are parameters that affect how much snapshot history the storage engine will keep to
 * support point-in-time transactions (read or write). This is referred to as the snapshot window.
 * The window is between the stable timestamp and the oldest timestamp.
 */
struct SnapshotWindowParams {

    // maxTargetSnapshotHistoryWindowInSeconds (startup & runtime server paramter, range 0+).
    //
    // Dictates the maximum lag in seconds oldest_timestamp should be behind stable_timestamp.
    // targetSnapshotHistoryWindowInSeconds below is the actual active lag setting target.
    //
    // Note that the window size can become greater than this if an ongoing operation is holding an
    // older snapshot open.
    AtomicWord<int> maxTargetSnapshotHistoryWindowInSeconds{5};

    // targetSnapshotHistoryWindowInSeconds (not a server parameter, range 0+).
    //
    // Dictates the target lag in seconds oldest_timestamp should be set behind stable_timestamp.
    // Should only be set in the range [0, maxTargetSnapshotHistoryWindowInSeconds].
    //
    // Note that this is the history window we attempt to maintain, but our current system state may
    // not always reflect it: the window can only change as more writes come in, so it can take time
    // for the actual window size to catch up with a change. This value guides actions whenever the
    // system goes to update the oldest_timestamp value (usually when the stable_timestamp is
    // updated).
    AtomicWord<int> targetSnapshotHistoryWindowInSeconds{
        maxTargetSnapshotHistoryWindowInSeconds.load()};

    // snapshotWindowMultiplicativeDecrease (startup & runtime server paramter, range (0,1)).
    //
    // Controls by what multiplier the target snapshot history window size setting is decreased
    // when there is cache pressure.
    AtomicDouble snapshotWindowMultiplicativeDecrease{0.75};

    // snapshotWindowAdditiveIncreaseSeconds (startup & runtime server paramter, range 1+).
    //
    // Controls by how much the target snapshot history window size setting is increased when we
    // need to service older snapshots for global point-in-time reads.
    AtomicWord<int> snapshotWindowAdditiveIncreaseSeconds{1};

    // minMillisBetweenSnapshotWindowInc (startup & runtime server paramter, range 0+).
    //
    // Controls how often attempting to increase the target snapshot window will have an effect.
    // Multiple callers within minMillisBetweenSnapshotWindowInc will have the same effect as one.
    // This protects the system because it takes time for the target snapshot window to affect the
    // actual storage engine snapshot window. The stable timestamp must move forward for the window
    // between it and oldest timestamp to grow or shrink.
    AtomicWord<int> minMillisBetweenSnapshotWindowInc{500};

    // decreaseHistoryIfNotNeededPeriodSeconds (startup & runtime server paramter, range 1+)
    //
    // Controls the period of the task that checks for cache pressure and decreases
    // targetSnapshotHistoryWindowInSeconds if there has been pressure and no new SnapshotTooOld
    // errors.
    //
    // This should not run very frequently. It is preferable to increase the window size, and cache
    // pressure, rather than failing PIT reads.
    AtomicWord<int> decreaseHistoryIfNotNeededPeriodSeconds{15};

    static inline MutableObserverRegistry<decltype(
        decreaseHistoryIfNotNeededPeriodSeconds)::WordType>
        observeDecreaseHistoryIfNotNeededPeriodSeconds;

    AtomicWord<long long> snapshotTooOldErrorCount{0};
};

extern SnapshotWindowParams snapshotWindowParams;

}  // namespace monger
