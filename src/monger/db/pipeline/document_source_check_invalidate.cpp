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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kCommand

#include "monger/platform/basic.h"

#include "monger/db/pipeline/document_source_change_stream.h"
#include "monger/db/pipeline/document_source_check_invalidate.h"
#include "monger/util/log.h"

namespace monger {

using DSCS = DocumentSourceChangeStream;

namespace {

// Returns true if the given 'operationType' should invalidate the change stream based on the
// namespace in 'pExpCtx'.
bool isInvalidatingCommand(const boost::intrusive_ptr<ExpressionContext>& pExpCtx,
                           StringData operationType) {
    if (pExpCtx->isSingleNamespaceAggregation()) {
        return operationType == DSCS::kDropCollectionOpType ||
            operationType == DSCS::kRenameCollectionOpType ||
            operationType == DSCS::kDropDatabaseOpType;
    } else if (!pExpCtx->isClusterAggregation()) {
        return operationType == DSCS::kDropDatabaseOpType;
    } else {
        return false;
    }
};

}  // namespace

DocumentSource::GetNextResult DocumentSourceCheckInvalidate::getNext() {
    pExpCtx->checkForInterrupt();

    if (_queuedInvalidate) {
        const auto res = DocumentSource::GetNextResult(std::move(_queuedInvalidate.get()));
        _queuedInvalidate.reset();
        return res;
    }

    auto nextInput = pSource->getNext();
    if (!nextInput.isAdvanced())
        return nextInput;

    auto doc = nextInput.getDocument();
    const auto& kOperationTypeField = DSCS::kOperationTypeField;
    DSCS::checkValueType(doc[kOperationTypeField], kOperationTypeField, BSONType::String);
    auto operationType = doc[kOperationTypeField].getString();

    // If this command should invalidate the stream, generate an invalidate entry and queue it up
    // to be returned after the notification of this command. The new entry will have a nearly
    // identical resume token to the notification for the command, except with an extra flag
    // indicating that the token is from an invalidate. This flag is necessary to disambiguate
    // the two tokens, and thus preserve a total ordering on the stream.

    // As a special case, if a client receives an invalidate like this one and then wants to
    // start a new stream after the invalidate, they can use the "startAfter" option, in which
    // case '_ignoreFirstInvalidate' will be set, and we should ignore (AKA not generate) the
    // very first invalidation.
    if (isInvalidatingCommand(pExpCtx, operationType) && !_ignoreFirstInvalidate) {
        auto resumeTokenData = ResumeToken::parse(doc[DSCS::kIdField].getDocument()).getData();
        resumeTokenData.fromInvalidate = ResumeTokenData::FromInvalidate::kFromInvalidate;
        auto resumeTokenDoc = ResumeToken(resumeTokenData).toDocument();

        MutableDocument result(Document{{DSCS::kIdField, resumeTokenDoc},
                                        {DSCS::kOperationTypeField, DSCS::kInvalidateOpType},
                                        {DSCS::kClusterTimeField, doc[DSCS::kClusterTimeField]}});
        result.copyMetaDataFrom(doc);

        // We set the resume token as the document's sort key in both the sharded and non-sharded
        // cases, since we will later rely upon it to generate a correct postBatchResumeToken. We
        // must therefore update the sort key to match the new resume token that we generated above.
        result.setSortKeyMetaField(resumeTokenDoc.toBson());

        _queuedInvalidate = result.freeze();
    }

    // Regardless of whether the first document we see is an invalidating command, we only skip the
    // first invalidate for streams with the 'startAfter' option, so we should not skip any
    // invalidates that come after the first one.
    _ignoreFirstInvalidate = false;

    return nextInput;
}

}  // namespace monger
