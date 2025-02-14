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

#include "monger/db/repl/oplog_applier.h"
#include "monger/db/repl/replication_consistency_markers.h"
#include "monger/db/repl/replication_coordinator.h"
#include "monger/db/repl/storage_interface.h"
#include "monger/db/repl/sync_tail.h"

namespace monger {
namespace repl {

/**
 * Applies oplog entries.
 * Reads from an OplogBuffer batches of operations that may be applied in parallel.
 */
class OplogApplierImpl : public OplogApplier {
    OplogApplierImpl(const OplogApplierImpl&) = delete;
    OplogApplierImpl& operator=(const OplogApplierImpl&) = delete;

public:
    /**
     * Constructs this OplogApplier with specific options.
     * Obtains batches of operations from the OplogBuffer to apply.
     * Reports oplog application progress using the Observer.
     */
    OplogApplierImpl(executor::TaskExecutor* executor,
                     OplogBuffer* oplogBuffer,
                     Observer* observer,
                     ReplicationCoordinator* replCoord,
                     ReplicationConsistencyMarkers* consistencyMarkers,
                     StorageInterface* storageInterface,
                     const Options& options,
                     ThreadPool* writerPool);

private:
    void _run(OplogBuffer* oplogBuffer) override;

    void _shutdown() override;

    StatusWith<OpTime> _multiApply(OperationContext* opCtx,
                                   Operations ops,
                                   boost::optional<repl::OplogApplication::Mode> mode) override;

    // Not owned by us.
    ReplicationCoordinator* const _replCoord;

    // Used to run oplog application loop.
    SyncTail _syncTail;

    // Used to determine which operations should be applied during initial sync. If this is null,
    // we will apply all operations that were fetched.
    OpTime _beginApplyingOpTime = OpTime();
};

}  // namespace repl
}  // namespace monger
