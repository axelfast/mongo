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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kReplication

#include "monger/platform/basic.h"

#include "monger/db/repl/data_replicator_external_state_impl.h"

#include "monger/base/init.h"
#include "monger/db/repl/oplog_applier_impl.h"
#include "monger/db/repl/oplog_buffer_blocking_queue.h"
#include "monger/db/repl/oplog_buffer_collection.h"
#include "monger/db/repl/oplog_buffer_proxy.h"
#include "monger/db/repl/repl_server_parameters_gen.h"
#include "monger/db/repl/replication_coordinator.h"
#include "monger/db/repl/replication_coordinator_external_state.h"
#include "monger/db/repl/replication_process.h"
#include "monger/db/repl/storage_interface.h"
#include "monger/util/log.h"

namespace monger {
namespace repl {
namespace {

const char kCollectionOplogBufferName[] = "collection";
const char kBlockingQueueOplogBufferName[] = "inMemoryBlockingQueue";

MONGO_INITIALIZER(initialSyncOplogBuffer)(InitializerContext*) {
    if ((initialSyncOplogBuffer != kCollectionOplogBufferName) &&
        (initialSyncOplogBuffer != kBlockingQueueOplogBufferName)) {
        return Status(ErrorCodes::BadValue,
                      "unsupported initial sync oplog buffer option: " + initialSyncOplogBuffer);
    }
    return Status::OK();
}

}  // namespace

DataReplicatorExternalStateImpl::DataReplicatorExternalStateImpl(
    ReplicationCoordinator* replicationCoordinator,
    ReplicationCoordinatorExternalState* replicationCoordinatorExternalState)
    : _replicationCoordinator(replicationCoordinator),
      _replicationCoordinatorExternalState(replicationCoordinatorExternalState) {}

executor::TaskExecutor* DataReplicatorExternalStateImpl::getTaskExecutor() const {
    return _replicationCoordinatorExternalState->getTaskExecutor();
}

OpTimeWithTerm DataReplicatorExternalStateImpl::getCurrentTermAndLastCommittedOpTime() {
    return {_replicationCoordinator->getTerm(), _replicationCoordinator->getLastCommittedOpTime()};
}

void DataReplicatorExternalStateImpl::processMetadata(const rpc::ReplSetMetadata& replMetadata,
                                                      rpc::OplogQueryMetadata oqMetadata) {
    OpTimeAndWallTime newCommitPoint = oqMetadata.getLastOpCommitted();

    const bool fromSyncSource = true;
    _replicationCoordinator->advanceCommitPoint(newCommitPoint, fromSyncSource);

    _replicationCoordinator->processReplSetMetadata(replMetadata);

    if ((oqMetadata.getPrimaryIndex() != rpc::OplogQueryMetadata::kNoPrimary) ||
        (replMetadata.getPrimaryIndex() != rpc::ReplSetMetadata::kNoPrimary)) {
        _replicationCoordinator->cancelAndRescheduleElectionTimeout();
    }
}

bool DataReplicatorExternalStateImpl::shouldStopFetching(
    const HostAndPort& source,
    const rpc::ReplSetMetadata& replMetadata,
    boost::optional<rpc::OplogQueryMetadata> oqMetadata) {
    // Re-evaluate quality of sync target.
    if (_replicationCoordinator->shouldChangeSyncSource(source, replMetadata, oqMetadata)) {
        // If OplogQueryMetadata was provided, its values were used to determine if we should
        // change sync sources.
        if (oqMetadata) {
            log() << "Canceling oplog query due to OplogQueryMetadata. We have to choose a new "
                     "sync source. Current source: "
                  << source << ", OpTime " << oqMetadata->getLastOpApplied()
                  << ", its sync source index:" << oqMetadata->getSyncSourceIndex();

        } else {
            log() << "Canceling oplog query due to ReplSetMetadata. We have to choose a new sync "
                     "source. Current source: "
                  << source << ", OpTime " << replMetadata.getLastOpVisible()
                  << ", its sync source index:" << replMetadata.getSyncSourceIndex();
        }
        return true;
    }
    return false;
}

std::unique_ptr<OplogBuffer> DataReplicatorExternalStateImpl::makeInitialSyncOplogBuffer(
    OperationContext* opCtx) const {
    if (initialSyncOplogBuffer == kCollectionOplogBufferName) {
        invariant(initialSyncOplogBufferPeekCacheSize >= 0);
        OplogBufferCollection::Options options;
        options.peekCacheSize = std::size_t(initialSyncOplogBufferPeekCacheSize);
        return std::make_unique<OplogBufferProxy>(
            std::make_unique<OplogBufferCollection>(StorageInterface::get(opCtx), options));
    } else {
        return std::make_unique<OplogBufferBlockingQueue>();
    }
}

std::unique_ptr<OplogApplier> DataReplicatorExternalStateImpl::makeOplogApplier(
    OplogBuffer* oplogBuffer,
    OplogApplier::Observer* observer,
    ReplicationConsistencyMarkers* consistencyMarkers,
    StorageInterface* storageInterface,
    const OplogApplier::Options& options,
    ThreadPool* writerPool) {
    return std::make_unique<OplogApplierImpl>(getTaskExecutor(),
                                              oplogBuffer,
                                              observer,
                                              _replicationCoordinator,
                                              consistencyMarkers,
                                              storageInterface,
                                              options,
                                              writerPool);
}

StatusWith<ReplSetConfig> DataReplicatorExternalStateImpl::getCurrentConfig() const {
    return _replicationCoordinator->getConfig();
}

ReplicationCoordinator* DataReplicatorExternalStateImpl::getReplicationCoordinator() const {
    return _replicationCoordinator;
}

ReplicationCoordinatorExternalState*
DataReplicatorExternalStateImpl::getReplicationCoordinatorExternalState() const {
    return _replicationCoordinatorExternalState;
}

}  // namespace repl
}  // namespace monger
