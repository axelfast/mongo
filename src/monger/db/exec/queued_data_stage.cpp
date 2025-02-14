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

#include "monger/db/exec/queued_data_stage.h"

#include <memory>

#include "monger/db/exec/scoped_timer.h"
#include "monger/db/exec/working_set_common.h"

namespace monger {

using std::unique_ptr;
using std::vector;

const char* QueuedDataStage::kStageType = "QUEUED_DATA";

QueuedDataStage::QueuedDataStage(OperationContext* opCtx, WorkingSet* ws)
    : PlanStage(kStageType, opCtx), _ws(ws) {}

PlanStage::StageState QueuedDataStage::doWork(WorkingSetID* out) {
    if (isEOF()) {
        return PlanStage::IS_EOF;
    }

    StageState state = _results.front();
    _results.pop();

    switch (state) {
        case PlanStage::ADVANCED:
            *out = _members.front();
            _members.pop();
            break;
        case PlanStage::FAILURE:
            // On FAILURE, this stage is reponsible for allocating the WorkingSetMember with
            // the error details.
            *out = WorkingSetCommon::allocateStatusMember(
                _ws, Status(ErrorCodes::InternalError, "Queued data stage failure"));
            break;
        default:
            break;
    }

    return state;
}

bool QueuedDataStage::isEOF() {
    return _results.empty();
}

unique_ptr<PlanStageStats> QueuedDataStage::getStats() {
    _commonStats.isEOF = isEOF();
    unique_ptr<PlanStageStats> ret =
        std::make_unique<PlanStageStats>(_commonStats, STAGE_QUEUED_DATA);
    ret->specific = std::make_unique<MockStats>(_specificStats);
    return ret;
}


const SpecificStats* QueuedDataStage::getSpecificStats() const {
    return &_specificStats;
}

void QueuedDataStage::pushBack(const PlanStage::StageState state) {
    invariant(PlanStage::ADVANCED != state);
    _results.push(state);
}

void QueuedDataStage::pushBack(const WorkingSetID& id) {
    _results.push(PlanStage::ADVANCED);

    // member lives in _ws.  We'll return it when _results hits ADVANCED.
    _members.push(id);
}

}  // namespace monger
