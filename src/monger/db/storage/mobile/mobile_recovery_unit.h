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
#include <string>
#include <vector>

#include "monger/base/checked_cast.h"
#include "monger/db/operation_context.h"
#include "monger/db/record_id.h"
#include "monger/db/storage/mobile/mobile_session.h"
#include "monger/db/storage/mobile/mobile_session_pool.h"
#include "monger/db/storage/recovery_unit.h"
#include "monger/db/storage/snapshot.h"
#include "monger/platform/atomic_word.h"

namespace monger {

class SortedDataInterface;

class MobileRecoveryUnit final : public RecoveryUnit {
public:
    MobileRecoveryUnit(MobileSessionPool* sessionPool);
    virtual ~MobileRecoveryUnit();

    void beginUnitOfWork(OperationContext* opCtx) override;
    void commitUnitOfWork() override;
    void abortUnitOfWork() override;
    bool waitUntilDurable() override;

    void abandonSnapshot() override;

    SnapshotId getSnapshotId() const override {
        return SnapshotId();
    }

    MobileSession* getSession(OperationContext* opCtx, bool readOnly = true);

    MobileSession* getSessionNoTxn(OperationContext* opCtx);

    bool inActiveTxn() const {
        return _isActive();
    }

    void assertInActiveTxn() const;

    void enqueueFailedDrop(std::string& dropQuery);

    static MobileRecoveryUnit* get(OperationContext* opCtx) {
        return checked_cast<MobileRecoveryUnit*>(opCtx->recoveryUnit());
    }

    void setOrderedCommit(bool orderedCommit) override {}

private:
    void _abort();
    void _commit();

    void _ensureSession(OperationContext* opCtx);
    void _txnClose(bool commit);
    void _txnOpen(OperationContext* opCtx, bool readOnly);
    void _upgradeToWriteSession(OperationContext* opCtx);

    static AtomicWord<long long> _nextID;
    uint64_t _id;
    bool _isReadOnly;

    std::string _path;
    MobileSessionPool* _sessionPool;
    std::unique_ptr<MobileSession> _session;
};

}  // namespace monger
