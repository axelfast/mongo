/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include "monger/platform/basic.h"

#include "monger/db/repl/timestamp_block.h"

#include "monger/db/storage/recovery_unit.h"
#include "monger/db/storage/storage_options.h"
#include "monger/util/assert_util.h"

namespace monger {

TimestampBlock::TimestampBlock(OperationContext* opCtx, Timestamp ts) : _opCtx(opCtx), _ts(ts) {
    uassert(ErrorCodes::IllegalOperation,
            "Cannot timestamp a write operation in read-only mode",
            !storageGlobalParams.readOnly);
    if (!_ts.isNull()) {
        _opCtx->recoveryUnit()->setCommitTimestamp(_ts);
    }
}

TimestampBlock::~TimestampBlock() {
    if (!_ts.isNull()) {
        _opCtx->recoveryUnit()->clearCommitTimestamp();
    }
}

}  // namespace monger
