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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kSharding

#include "monger/platform/basic.h"

#include "monger/s/catalog/dist_lock_manager.h"

#include <memory>

namespace monger {

const Seconds DistLockManager::kDefaultLockTimeout(20);
const Milliseconds DistLockManager::kSingleLockAttemptTimeout(0);

DistLockManager::ScopedDistLock::ScopedDistLock(OperationContext* opCtx,
                                                DistLockHandle lockHandle,
                                                DistLockManager* lockManager)
    : _opCtx(opCtx), _lockID(std::move(lockHandle)), _lockManager(lockManager) {}

DistLockManager::ScopedDistLock::~ScopedDistLock() {
    if (_lockManager) {
        _lockManager->unlock(_opCtx, _lockID);
    }
}

DistLockManager::ScopedDistLock::ScopedDistLock(ScopedDistLock&& other)
    : _opCtx(nullptr), _lockManager(nullptr) {
    *this = std::move(other);
}

DistLockManager::ScopedDistLock& DistLockManager::ScopedDistLock::operator=(
    ScopedDistLock&& other) {
    if (this != &other) {
        invariant(_lockManager == nullptr);
        invariant(_opCtx == nullptr);

        _opCtx = other._opCtx;
        _lockID = std::move(other._lockID);
        _lockManager = other._lockManager;
        other._lockManager = nullptr;
    }

    return *this;
}

StatusWith<DistLockManager::ScopedDistLock> DistLockManager::lock(OperationContext* opCtx,
                                                                  StringData name,
                                                                  StringData whyMessage,
                                                                  Milliseconds waitFor) {
    auto distLockHandleStatus = lockWithSessionID(opCtx, name, whyMessage, OID::gen(), waitFor);
    if (!distLockHandleStatus.isOK()) {
        return distLockHandleStatus.getStatus();
    }

    return DistLockManager::ScopedDistLock(opCtx, std::move(distLockHandleStatus.getValue()), this);
}

Status DistLockManager::ScopedDistLock::checkStatus() {
    if (!_lockManager) {
        return Status(ErrorCodes::IllegalOperation, "no lock manager, lock was not acquired");
    }

    return _lockManager->checkStatus(_opCtx, _lockID);
}

}  // namespace monger
