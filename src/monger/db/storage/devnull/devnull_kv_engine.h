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

#include <memory>

#include "monger/db/storage/kv/kv_engine.h"
#include "monger/db/storage/recovery_unit_noop.h"

namespace monger {

class JournalListener;

/**
 * The devnull storage engine is intended for unit and performance testing.
 */
class DevNullKVEngine : public KVEngine {
public:
    virtual ~DevNullKVEngine() {}

    virtual RecoveryUnit* newRecoveryUnit() {
        return new RecoveryUnitNoop();
    }

    virtual Status createRecordStore(OperationContext* opCtx,
                                     StringData ns,
                                     StringData ident,
                                     const CollectionOptions& options) {
        return Status::OK();
    }

    virtual std::unique_ptr<RecordStore> getRecordStore(OperationContext* opCtx,
                                                        StringData ns,
                                                        StringData ident,
                                                        const CollectionOptions& options);

    virtual std::unique_ptr<RecordStore> makeTemporaryRecordStore(OperationContext* opCtx,
                                                                  StringData ident) override;

    virtual Status createSortedDataInterface(OperationContext* opCtx,
                                             const CollectionOptions& collOptions,
                                             StringData ident,
                                             const IndexDescriptor* desc) {
        return Status::OK();
    }

    virtual std::unique_ptr<SortedDataInterface> getSortedDataInterface(
        OperationContext* opCtx, StringData ident, const IndexDescriptor* desc);

    virtual Status dropIdent(OperationContext* opCtx, StringData ident) {
        return Status::OK();
    }

    virtual bool supportsDocLocking() const {
        return true;
    }

    virtual bool supportsDirectoryPerDB() const {
        return false;
    }

    /**
     * devnull does no journaling, so don't report the engine as durable.
     */
    virtual bool isDurable() const {
        return false;
    }

    virtual bool isEphemeral() const {
        return true;
    }

    virtual int64_t getCacheOverflowTableInsertCount(OperationContext* opCtx) const override;

    virtual void setCacheOverflowTableInsertCountForTest(int insertCount) override;

    virtual int64_t getIdentSize(OperationContext* opCtx, StringData ident) {
        return 1;
    }

    virtual Status repairIdent(OperationContext* opCtx, StringData ident) {
        return Status::OK();
    }

    virtual bool hasIdent(OperationContext* opCtx, StringData ident) const {
        return true;
    }

    std::vector<std::string> getAllIdents(OperationContext* opCtx) const {
        return std::vector<std::string>();
    }

    virtual void cleanShutdown(){};

    void setJournalListener(JournalListener* jl) final {}

    virtual Timestamp getAllCommittedTimestamp() const override {
        return Timestamp();
    }

    virtual Timestamp getOldestOpenReadTimestamp() const override {
        return Timestamp();
    }

    boost::optional<Timestamp> getOplogNeededForCrashRecovery() const final {
        return boost::none;
    }

    virtual Status beginBackup(OperationContext* opCtx) override {
        return Status::OK();
    }

    virtual void endBackup(OperationContext* opCtx) {}

    virtual StatusWith<std::vector<std::string>> beginNonBlockingBackup(
        OperationContext* opCtx) override;

    virtual void endNonBlockingBackup(OperationContext* opCtx) override {}

    virtual StatusWith<std::vector<std::string>> extendBackupCursor(
        OperationContext* opCtx) override;

    virtual boost::optional<Timestamp> getLastStableRecoveryTimestamp() const override {
        return boost::none;
    }

private:
    std::shared_ptr<void> _catalogInfo;

    int _overflowTableInsertCountForTest;
};
}
