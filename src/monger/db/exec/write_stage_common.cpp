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

#include "monger/db/exec/write_stage_common.h"

#include "monger/db/catalog/collection.h"
#include "monger/db/concurrency/write_conflict_exception.h"
#include "monger/db/exec/working_set.h"
#include "monger/db/exec/working_set_common.h"
#include "monger/db/operation_context.h"
#include "monger/db/query/canonical_query.h"

namespace monger {
namespace write_stage_common {

bool ensureStillMatches(const Collection* collection,
                        OperationContext* opCtx,
                        WorkingSet* ws,
                        WorkingSetID id,
                        const CanonicalQuery* cq) {
    // If the snapshot changed, then we have to make sure we have the latest copy of the doc and
    // that it still matches.
    //
    // Storage engines that don't support document-level concurrency also do not track snapshot ids.
    // Those storage engines always need to check whether the document still matches, as the
    // document we are planning to delete may have already been deleted or updated during yield.
    WorkingSetMember* member = ws->get(id);
    if (!supportsDocLocking() ||
        opCtx->recoveryUnit()->getSnapshotId() != member->obj.snapshotId()) {
        std::unique_ptr<SeekableRecordCursor> cursor(collection->getCursor(opCtx));

        if (!WorkingSetCommon::fetch(opCtx, ws, id, cursor)) {
            // Doc is already deleted.
            return false;
        }

        // Make sure the re-fetched doc still matches the predicate.
        if (cq && !cq->root()->matchesBSON(member->obj.value(), nullptr)) {
            // No longer matches.
            return false;
        }

        // Ensure that the BSONObj underlying the WorkingSetMember is owned because the cursor's
        // destructor is allowed to free the memory.
        member->makeObjOwnedIfNeeded();
    }
    return true;
}

}  // namespace write_stage_common
}  // namespace monger
