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

#include "monger/db/exec/text_match.h"

#include <memory>
#include <vector>

#include "monger/db/exec/scoped_timer.h"
#include "monger/db/exec/working_set.h"
#include "monger/db/exec/working_set_common.h"
#include "monger/db/jsobj.h"
#include "monger/util/str.h"

namespace monger {

using std::unique_ptr;
using std::vector;

const char* TextMatchStage::kStageType = "TEXT_MATCH";

TextMatchStage::TextMatchStage(OperationContext* opCtx,
                               unique_ptr<PlanStage> child,
                               const FTSQueryImpl& query,
                               const FTSSpec& spec,
                               WorkingSet* ws)
    : PlanStage(kStageType, opCtx), _ftsMatcher(query, spec), _ws(ws) {
    _children.emplace_back(std::move(child));
}

TextMatchStage::~TextMatchStage() {}

bool TextMatchStage::isEOF() {
    return child()->isEOF();
}

std::unique_ptr<PlanStageStats> TextMatchStage::getStats() {
    _commonStats.isEOF = isEOF();

    unique_ptr<PlanStageStats> ret =
        std::make_unique<PlanStageStats>(_commonStats, STAGE_TEXT_MATCH);
    ret->specific = std::make_unique<TextMatchStats>(_specificStats);
    ret->children.emplace_back(child()->getStats());

    return ret;
}

const SpecificStats* TextMatchStage::getSpecificStats() const {
    return &_specificStats;
}

PlanStage::StageState TextMatchStage::doWork(WorkingSetID* out) {
    if (isEOF()) {
        return PlanStage::IS_EOF;
    }

    // Retrieve fetched document from child.
    StageState stageState = child()->work(out);

    if (stageState == PlanStage::ADVANCED) {
        // We just successfully retrieved a fetched doc.
        WorkingSetMember* wsm = _ws->get(*out);

        // Filter for phrases and negated terms.
        if (!_ftsMatcher.matches(wsm->obj.value())) {
            _ws->free(*out);
            *out = WorkingSet::INVALID_ID;
            ++_specificStats.docsRejected;
            stageState = PlanStage::NEED_TIME;
        }
    } else if (stageState == PlanStage::FAILURE) {
        // If a stage fails, it may create a status WSM to indicate why it
        // failed, in which case '*out' is valid.  If ID is invalid, we
        // create our own error message.
        if (WorkingSet::INVALID_ID == *out) {
            str::stream ss;
            ss << "TEXT_MATCH stage failed to read in results from child";
            Status status(ErrorCodes::InternalError, ss);
            *out = WorkingSetCommon::allocateStatusMember(_ws, status);
        }
    }

    return stageState;
}

}  // namespace monger
