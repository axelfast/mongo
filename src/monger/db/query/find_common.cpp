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

#include "monger/platform/basic.h"

#include "monger/db/query/find_common.h"

#include "monger/bson/bsonobj.h"
#include "monger/db/curop.h"
#include "monger/db/query/query_request.h"
#include "monger/util/assert_util.h"

namespace monger {

MONGO_FAIL_POINT_DEFINE(waitInFindBeforeMakingBatch);

MONGO_FAIL_POINT_DEFINE(disableAwaitDataForGetMoreCmd);

MONGO_FAIL_POINT_DEFINE(waitAfterPinningCursorBeforeGetMoreBatch);

MONGO_FAIL_POINT_DEFINE(waitWithPinnedCursorDuringGetMoreBatch);

MONGO_FAIL_POINT_DEFINE(waitBeforeUnpinningOrDeletingCursorAfterGetMoreBatch);

const OperationContext::Decoration<AwaitDataState> awaitDataState =
    OperationContext::declareDecoration<AwaitDataState>();

bool FindCommon::enoughForFirstBatch(const QueryRequest& qr, long long numDocs) {
    if (!qr.getEffectiveBatchSize()) {
        // We enforce a default batch size for the initial find if no batch size is specified.
        return numDocs >= QueryRequest::kDefaultBatchSize;
    }

    return numDocs >= qr.getEffectiveBatchSize().value();
}

bool FindCommon::haveSpaceForNext(const BSONObj& nextDoc, long long numDocs, int bytesBuffered) {
    invariant(numDocs >= 0);
    if (!numDocs) {
        // Allow the first output document to exceed the limit to ensure we can always make
        // progress.
        return true;
    }

    return (bytesBuffered + nextDoc.objsize()) <= kMaxBytesToReturnToClientAtOnce;
}

BSONObj FindCommon::transformSortSpec(const BSONObj& sortSpec) {
    BSONObjBuilder comparatorBob;

    for (BSONElement elt : sortSpec) {
        if (elt.isNumber()) {
            comparatorBob.append(elt);
        } else if (QueryRequest::isTextScoreMeta(elt)) {
            // Sort text score decreasing by default. Field name doesn't matter but we choose
            // something that a user shouldn't ever have.
            comparatorBob.append("$metaTextScore", -1);
        } else {
            // Sort spec should have been validated before here.
            fassertFailed(28784);
        }
    }

    return comparatorBob.obj();
}
}  // namespace monger
