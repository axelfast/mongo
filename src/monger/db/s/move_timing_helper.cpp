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

#include "monger/db/s/move_timing_helper.h"

#include "monger/db/client.h"
#include "monger/db/curop.h"
#include "monger/db/s/sharding_logging.h"
#include "monger/s/grid.h"
#include "monger/util/log.h"

namespace monger {

MoveTimingHelper::MoveTimingHelper(OperationContext* opCtx,
                                   const std::string& where,
                                   const std::string& ns,
                                   const BSONObj& min,
                                   const BSONObj& max,
                                   int totalNumSteps,
                                   std::string* cmdErrmsg,
                                   const ShardId& toShard,
                                   const ShardId& fromShard)
    : _opCtx(opCtx),
      _where(where),
      _ns(ns),
      _to(toShard),
      _from(fromShard),
      _totalNumSteps(totalNumSteps),
      _cmdErrmsg(cmdErrmsg),
      _nextStep(0) {
    _b.append("min", min);
    _b.append("max", max);
}

MoveTimingHelper::~MoveTimingHelper() {
    // even if logChange doesn't throw, bson does
    // sigh
    try {
        if (_to.isValid()) {
            _b.append("to", _to.toString());
        }

        if (_from.isValid()) {
            _b.append("from", _from.toString());
        }

        if (_nextStep != _totalNumSteps) {
            _b.append("note", "aborted");
        } else {
            _b.append("note", "success");
        }

        if (!_cmdErrmsg->empty()) {
            _b.append("errmsg", *_cmdErrmsg);
        }

        ShardingLogging::get(_opCtx)->logChange(_opCtx,
                                                str::stream() << "moveChunk." << _where,
                                                _ns,
                                                _b.obj(),
                                                ShardingCatalogClient::kMajorityWriteConcern);
    } catch (const std::exception& e) {
        warning() << "couldn't record timing for moveChunk '" << _where
                  << "': " << redact(e.what());
    }
}

void MoveTimingHelper::done(int step) {
    invariant(step == ++_nextStep);
    invariant(step <= _totalNumSteps);

    const std::string s = str::stream() << "step " << step << " of " << _totalNumSteps;

    CurOp* op = CurOp::get(_opCtx);

    {
        stdx::lock_guard<Client> lk(*_opCtx->getClient());
        op->setMessage_inlock(s.c_str());
    }

    _b.appendNumber(s, _t.millis());
    _t.reset();
}

}  // namespace monger
