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

#include "monger/db/storage/ephemeral_for_test/ephemeral_for_test_recovery_unit.h"

#include "monger/db/storage/sorted_data_interface.h"
#include "monger/util/log.h"

namespace monger {

EphemeralForTestRecoveryUnit::~EphemeralForTestRecoveryUnit() {
    invariant(!_inUnitOfWork(), toString(_getState()));
}

void EphemeralForTestRecoveryUnit::beginUnitOfWork(OperationContext* opCtx) {
    invariant(!_inUnitOfWork(), toString(_getState()));
    _setState(State::kInactiveInUnitOfWork);
}

void EphemeralForTestRecoveryUnit::commitUnitOfWork() {
    invariant(_inUnitOfWork(), toString(_getState()));
    _setState(State::kCommitting);

    try {
        for (Changes::iterator it = _changes.begin(), end = _changes.end(); it != end; ++it) {
            (*it)->commit(boost::none);
        }
        _changes.clear();
    } catch (...) {
        std::terminate();
    }

    // This ensures that the journal listener gets called on each commit.
    // SERVER-22575: Remove this once we add a generic mechanism to periodically wait
    // for durability.
    waitUntilDurable();
    _setState(State::kInactive);
}

void EphemeralForTestRecoveryUnit::abortUnitOfWork() {
    invariant(_inUnitOfWork(), toString(_getState()));
    _setState(State::kAborting);

    try {
        for (Changes::reverse_iterator it = _changes.rbegin(), end = _changes.rend(); it != end;
             ++it) {
            ChangePtr change = *it;
            LOG(2) << "CUSTOM ROLLBACK " << demangleName(typeid(*change));
            change->rollback();
        }
        _changes.clear();
    } catch (...) {
        std::terminate();
    }

    _setState(State::kInactive);
}

bool EphemeralForTestRecoveryUnit::waitUntilDurable() {
    if (_waitUntilDurableCallback) {
        _waitUntilDurableCallback();
    }
    return true;
}

bool EphemeralForTestRecoveryUnit::inActiveTxn() const {
    return _inUnitOfWork();
}

void EphemeralForTestRecoveryUnit::abandonSnapshot() {
    invariant(!_inUnitOfWork(), toString(_getState()));
}

Status EphemeralForTestRecoveryUnit::obtainMajorityCommittedSnapshot() {
    return Status::OK();
}

void EphemeralForTestRecoveryUnit::registerChange(Change* change) {
    _changes.push_back(ChangePtr(change));
}
}  // namespace monger
