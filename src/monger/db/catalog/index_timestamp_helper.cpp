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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kIndex

#include "monger/db/catalog/index_timestamp_helper.h"

#include "monger/db/concurrency/write_conflict_exception.h"
#include "monger/db/logical_clock.h"
#include "monger/db/repl/member_state.h"
#include "monger/db/repl/replication_coordinator.h"
#include "monger/util/log.h"

namespace monger {

namespace {
bool requiresGhostCommitTimestampForWrite(OperationContext* opCtx, const NamespaceString& nss) {
    if (!nss.isReplicated()) {
        return false;
    }

    auto replCoord = repl::ReplicationCoordinator::get(opCtx);
    if (!replCoord->isReplEnabled()) {
        return false;
    }

    if (replCoord->getMemberState().startup2()) {
        return false;
    }

    // Only storage engines that support recover-to-stable need ghost commit timestamps.
    if (!opCtx->getServiceContext()->getStorageEngine()->supportsRecoveryTimestamp()) {
        return false;
    }

    return true;
}
}  // namespace

void IndexTimestampHelper::setGhostCommitTimestampForWrite(OperationContext* opCtx,
                                                           const NamespaceString& nss) {
    invariant(opCtx->lockState()->inAWriteUnitOfWork());

    if (!requiresGhostCommitTimestampForWrite(opCtx, nss)) {
        return;
    }

    // The lastApplied timestamp is the last OpTime that a node has applied. We choose this
    // timestamp on primaries because it is the most recent point-in-time a reader would be able to
    // to read at, despite it lagging slighly behind recently committed writes. Because of this lag,
    // both on primaries and secondaries, the lastApplied time may be older than any newly committed
    // writes. It is therefore required that all callers holding intent locks and wishing to apply
    // ghost timestamps also establish a storage engine snapshot for reading that is less than or
    // equal to the lastApplied timestamp.
    const auto replCoord = repl::ReplicationCoordinator::get(opCtx);
    const auto commitTimestamp = replCoord->getMyLastAppliedOpTime().getTimestamp();

    const auto mySnapshot = opCtx->recoveryUnit()->getPointInTimeReadTimestamp();
    // If a lock that blocks writes is held, there can be no uncommitted writes, so there is no
    // need to check snapshot visibility, especially if a caller is not reading with a timestamp.
    invariant(mySnapshot || opCtx->lockState()->isCollectionLockedForMode(nss, MODE_S),
              "a write-blocking lock is required when applying a ghost timestamp without a read "
              "timestamp");
    invariant(!mySnapshot || *mySnapshot <= commitTimestamp,
              str::stream() << "commit timestamp " << commitTimestamp.toString()
                            << " cannot be older than current read timestamp "
                            << mySnapshot->toString());

    auto status = opCtx->recoveryUnit()->setTimestamp(commitTimestamp);
    if (status.code() == ErrorCodes::BadValue) {
        log() << "Temporarily could not apply ghost commit timestamp. " << status.reason();
        throw WriteConflictException();
    }
    LOG(1) << "assigning ghost commit timestamp: " << commitTimestamp.toString();

    fassert(51053, status);
}

/**
 * Returns true if writes to the catalog entry for the input namespace require being
 * timestamped. A ghost write is when the operation is not committed with an oplog entry and
 * implies the caller will look at the logical clock to choose a time to use.
 */
namespace {
bool requiresGhostCommitTimestampForCatalogWrite(OperationContext* opCtx, NamespaceString nss) {
    if (opCtx->writesAreReplicated()) {
        return false;
    }

    if (!nss.isReplicated() || nss.coll().startsWith("tmp.mr.")) {
        return false;
    }

    auto replCoord = repl::ReplicationCoordinator::get(opCtx);
    if (!replCoord->isReplEnabled()) {
        return false;
    }

    // If there is a commit timestamp already assigned, there's no need to explicitly assign a
    // timestamp. This case covers foreground index builds.
    if (!opCtx->recoveryUnit()->getCommitTimestamp().isNull()) {
        return false;
    }

    // Only oplog entries (including a user's `applyOps` command) construct indexes via
    // `IndexBuilder`. Nodes in `startup` may not yet have initialized the `LogicalClock`, however
    // index builds during startup replication recovery must be timestamped. These index builds
    // are foregrounded and timestamp their catalog writes with a "commit timestamp". Nodes in the
    // oplog application phase of initial sync (`startup2`) must not timestamp index builds before
    // the `initialDataTimestamp`.
    const auto memberState = replCoord->getMemberState();
    if (memberState.startup() || memberState.startup2()) {
        return false;
    }

    // When in rollback via refetch, it's valid for all writes to be untimestamped. Additionally,
    // it's illegal to timestamp a write later than the timestamp associated with the node exiting
    // the rollback state. This condition opts for being conservative.
    if (!serverGlobalParams.enableMajorityReadConcern && memberState.rollback()) {
        return false;
    }

    return true;
}
}  // namespace

void IndexTimestampHelper::setGhostCommitTimestampForCatalogWrite(OperationContext* opCtx,
                                                                  const NamespaceString& nss) {
    invariant(opCtx->lockState()->inAWriteUnitOfWork());
    if (!requiresGhostCommitTimestampForCatalogWrite(opCtx, nss)) {
        return;
    }
    auto status = opCtx->recoveryUnit()->setTimestamp(
        LogicalClock::get(opCtx)->getClusterTime().asTimestamp());
    if (status.code() == ErrorCodes::BadValue) {
        log() << "Temporarily could not timestamp the index build commit, retrying. "
              << status.reason();
        throw WriteConflictException();
    }
    fassert(50701, status);
}
}  // namespace monger
