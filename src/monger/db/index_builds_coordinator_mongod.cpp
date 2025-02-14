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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kStorage

#include "monger/platform/basic.h"

#include "monger/db/index_builds_coordinator_mongerd.h"

#include "monger/db/catalog/collection_catalog.h"
#include "monger/db/curop.h"
#include "monger/db/db_raii.h"
#include "monger/db/index_build_entry_helpers.h"
#include "monger/db/operation_context.h"
#include "monger/db/service_context.h"
#include "monger/util/assert_util.h"
#include "monger/util/log.h"
#include "monger/util/str.h"

namespace monger {

using namespace indexbuildentryhelpers;

namespace {

/**
 * Constructs the options for the loader thread pool.
 */
ThreadPool::Options makeDefaultThreadPoolOptions() {
    ThreadPool::Options options;
    options.poolName = "IndexBuildsCoordinatorMongerd";
    options.minThreads = 0;
    options.maxThreads = 10;

    // Ensure all threads have a client.
    options.onCreateThread = [](const std::string& threadName) {
        Client::initThread(threadName.c_str());
    };

    return options;
}

}  // namespace

IndexBuildsCoordinatorMongerd::IndexBuildsCoordinatorMongerd()
    : _threadPool(makeDefaultThreadPoolOptions()) {
    _threadPool.startup();
}

void IndexBuildsCoordinatorMongerd::shutdown() {
    // Stop new scheduling.
    _threadPool.shutdown();

    // Signal active builds to stop and wait for them to stop.
    interruptAllIndexBuildsForShutdown("Index build interrupted due to shutdown.");

    // Wait for active threads to finish.
    _threadPool.join();
}

StatusWith<SharedSemiFuture<ReplIndexBuildState::IndexCatalogStats>>
IndexBuildsCoordinatorMongerd::startIndexBuild(OperationContext* opCtx,
                                              CollectionUUID collectionUUID,
                                              const std::vector<BSONObj>& specs,
                                              const UUID& buildUUID,
                                              IndexBuildProtocol protocol,
                                              IndexBuildOptions indexBuildOptions) {
    auto statusWithOptionalResult = _registerAndSetUpIndexBuild(
        opCtx, collectionUUID, specs, buildUUID, protocol, indexBuildOptions.commitQuorum);
    if (!statusWithOptionalResult.isOK()) {
        return statusWithOptionalResult.getStatus();
    }

    if (statusWithOptionalResult.getValue()) {
        // TODO (SERVER-37644): when joining is implemented, the returned Future will no longer
        // always be set.
        invariant(statusWithOptionalResult.getValue()->isReady());
        // The requested index (specs) are already built or are being built. Return success early
        // (this is v4.0 behavior compatible).
        return statusWithOptionalResult.getValue().get();
    }

    auto replState = [&]() {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        auto it = _allIndexBuilds.find(buildUUID);
        invariant(it != _allIndexBuilds.end());
        return it->second;
    }();

    // Run index build in-line if we are transitioning between replication modes.
    // While the RSTLExclusive is being held, an async thread in the thread pool would not be
    // allowed to take locks.
    if (opCtx->lockState()->isRSTLExclusive()) {
        log() << "Running index build on current thread because we are transitioning between "
                 "replication states: "
              << buildUUID;
        // Sets up and runs the index build. Sets result and cleans up index build.
        _runIndexBuild(opCtx, buildUUID);
        return replState->sharedPromise.getFuture();
    }

    // Copy over all necessary OperationContext state.

    // Task in thread pool should retain the caller's deadline.
    const auto deadline = opCtx->getDeadline();
    const auto timeoutError = opCtx->getTimeoutError();

    // TODO: SERVER-39484 Because both 'writesAreReplicated' and
    // 'shouldNotConflictWithSecondaryBatchApplication' depend on the current replication state,
    // just passing the state here is not resilient to member state changes like stepup/stepdown.

    // If the calling thread is replicating oplog writes (primary), this state should be passed to
    // the builder.
    const bool writesAreReplicated = opCtx->writesAreReplicated();
    // Index builds on secondaries can't hold the PBWM lock because it would conflict with
    // replication.
    const bool shouldNotConflictWithSecondaryBatchApplication =
        !opCtx->lockState()->shouldConflictWithSecondaryBatchApplication();

    // Task in thread pool should have similar CurOp representation to the caller so that it can be
    // identified as a createIndexes operation.
    LogicalOp logicalOp = LogicalOp::opInvalid;
    BSONObj opDesc;
    {
        stdx::unique_lock<Client> lk(*opCtx->getClient());
        auto curOp = CurOp::get(opCtx);
        logicalOp = curOp->getLogicalOp();
        opDesc = curOp->opDescription().getOwned();
    }

    _threadPool.schedule([
        this,
        buildUUID,
        deadline,
        timeoutError,
        writesAreReplicated,
        shouldNotConflictWithSecondaryBatchApplication,
        logicalOp,
        opDesc,
        replState
    ](auto status) noexcept {
        // Clean up the index build if we failed to schedule it.
        if (!status.isOK()) {
            stdx::unique_lock<stdx::mutex> lk(_mutex);

            // Unregister the index build before setting the promises,
            // so callers do not see the build again.
            _unregisterIndexBuild(lk, replState);

            // Set the promise in case another thread already joined the index build.
            replState->sharedPromise.setError(status);

            return;
        }

        auto opCtx = Client::getCurrent()->makeOperationContext();

        opCtx->setDeadlineByDate(deadline, timeoutError);

        boost::optional<repl::UnreplicatedWritesBlock> unreplicatedWrites;
        if (!writesAreReplicated) {
            unreplicatedWrites.emplace(opCtx.get());
        }

        // If the calling thread should not take the PBWM lock, neither should this thread.
        boost::optional<ShouldNotConflictWithSecondaryBatchApplicationBlock> shouldNotConflictBlock;
        if (shouldNotConflictWithSecondaryBatchApplication) {
            shouldNotConflictBlock.emplace(opCtx->lockState());
        }

        {
            stdx::unique_lock<Client> lk(*opCtx->getClient());
            auto curOp = CurOp::get(opCtx.get());
            curOp->setLogicalOp_inlock(logicalOp);
            curOp->setOpDescription_inlock(opDesc);
        }

        // Sets up and runs the index build. Sets result and cleans up index build.
        _runIndexBuild(opCtx.get(), buildUUID);
    });


    return replState->sharedPromise.getFuture();
}

Status IndexBuildsCoordinatorMongerd::commitIndexBuild(OperationContext* opCtx,
                                                      const std::vector<BSONObj>& specs,
                                                      const UUID& buildUUID) {
    // TODO: not yet implemented.
    return Status::OK();
}

void IndexBuildsCoordinatorMongerd::signalChangeToPrimaryMode() {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    _replMode = ReplState::Primary;
}

void IndexBuildsCoordinatorMongerd::signalChangeToSecondaryMode() {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    _replMode = ReplState::Secondary;
}

void IndexBuildsCoordinatorMongerd::signalChangeToInitialSyncMode() {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    _replMode = ReplState::InitialSync;
}

Status IndexBuildsCoordinatorMongerd::voteCommitIndexBuild(const UUID& buildUUID,
                                                          const HostAndPort& hostAndPort) {
    // TODO: not yet implemented.
    return Status::OK();
}

Status IndexBuildsCoordinatorMongerd::setCommitQuorum(OperationContext* opCtx,
                                                     const NamespaceString& nss,
                                                     const std::vector<StringData>& indexNames,
                                                     const CommitQuorumOptions& newCommitQuorum) {
    if (indexNames.empty()) {
        return Status(ErrorCodes::IndexNotFound,
                      str::stream()
                          << "Cannot set a new commit quorum on an index build in collection '"
                          << nss
                          << "' without providing any indexes.");
    }

    AutoGetCollectionForRead autoColl(opCtx, nss);
    Collection* collection = autoColl.getCollection();
    if (!collection) {
        return Status(ErrorCodes::NamespaceNotFound,
                      str::stream() << "Collection '" << nss << "' was not found.");
    }

    UUID collectionUUID = *collection->uuid();

    stdx::unique_lock<stdx::mutex> lk(_mutex);
    auto collectionIt = _collectionIndexBuilds.find(collectionUUID);
    if (collectionIt == _collectionIndexBuilds.end()) {
        return Status(ErrorCodes::IndexNotFound,
                      str::stream() << "No index builds found on collection '" << nss << "'.");
    }

    if (!collectionIt->second->hasIndexBuildState(lk, indexNames.front())) {
        return Status(ErrorCodes::IndexNotFound,
                      str::stream() << "Cannot find an index build on collection '" << nss
                                    << "' with the provided index names");
    }

    // Use the first index to get the ReplIndexBuildState.
    std::shared_ptr<ReplIndexBuildState> buildState =
        collectionIt->second->getIndexBuildState(lk, indexNames.front());

    // Ensure the ReplIndexBuildState has the same indexes as 'indexNames'.
    bool equal = std::equal(
        buildState->indexNames.begin(), buildState->indexNames.end(), indexNames.begin());
    if (buildState->indexNames.size() != indexNames.size() || !equal) {
        return Status(ErrorCodes::IndexNotFound,
                      str::stream() << "Provided indexes are not all being "
                                    << "built by the same index builder in collection '"
                                    << nss
                                    << "'.");
    }

    // See if the new commit quorum is satisfiable.
    auto replCoord = repl::ReplicationCoordinator::get(opCtx);
    Status status = replCoord->checkIfCommitQuorumCanBeSatisfied(newCommitQuorum);
    if (!status.isOK()) {
        return status;
    }

    // Persist the new commit quorum for the index build and write it to the collection.
    buildState->commitQuorum = newCommitQuorum;
    // TODO (SERVER-40807): disabling the following code for the v4.2 release so it does not have
    // downstream impact.
    /*
    return indexbuildentryhelpers::setCommitQuorum(opCtx, buildState->buildUUID, newCommitQuorum);
    */
    return Status::OK();
}

Status IndexBuildsCoordinatorMongerd::_finishScanningPhase() {
    // TODO: implement.
    return Status::OK();
}

Status IndexBuildsCoordinatorMongerd::_finishVerificationPhase() {
    // TODO: implement.
    return Status::OK();
}

Status IndexBuildsCoordinatorMongerd::_finishCommitPhase() {
    // TODO: implement.
    return Status::OK();
}

StatusWith<bool> IndexBuildsCoordinatorMongerd::_checkCommitQuorum(
    const BSONObj& commitQuorum, const std::vector<HostAndPort>& confirmedMembers) {
    // TODO: not yet implemented.
    return false;
}

void IndexBuildsCoordinatorMongerd::_refreshReplStateFromPersisted(OperationContext* opCtx,
                                                                  const UUID& buildUUID) {
    // TODO: not yet implemented.
}

}  // namespace monger
