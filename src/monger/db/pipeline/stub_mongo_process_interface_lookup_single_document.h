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

#include <boost/intrusive_ptr.hpp>
#include <deque>
#include <vector>

#include "monger/db/exec/shard_filterer.h"
#include "monger/db/pipeline/document.h"
#include "monger/db/pipeline/document_source.h"
#include "monger/db/pipeline/pipeline.h"
#include "monger/db/pipeline/stub_monger_process_interface.h"

namespace monger {

class StubShardFilterer : public ShardFilterer {
public:
    DocumentBelongsResult documentBelongsToMe(const MatchableDocument& doc) const override {
        MONGO_UNREACHABLE;
    }

    bool isCollectionSharded() const override {
        MONGO_UNREACHABLE;
    }

    const KeyPattern& getKeyPattern() const override {
        MONGO_UNREACHABLE;
    }
};

/**
 * A mock MongerProcessInterface which allows mocking a foreign pipeline.
 */
class StubMongerProcessInterfaceLookupSingleDocument final : public StubMongerProcessInterface {
public:
    StubMongerProcessInterfaceLookupSingleDocument(
        std::deque<DocumentSource::GetNextResult> mockResults)
        : _mockResults(std::move(mockResults)) {}

    std::unique_ptr<Pipeline, PipelineDeleter> makePipeline(
        const std::vector<BSONObj>& rawPipeline,
        const boost::intrusive_ptr<ExpressionContext>& expCtx,
        const MakePipelineOptions opts = MakePipelineOptions{}) final;

    std::unique_ptr<Pipeline, PipelineDeleter> attachCursorSourceToPipeline(
        const boost::intrusive_ptr<ExpressionContext>& expCtx, Pipeline* ownedPipeline) final;
    std::unique_ptr<Pipeline, PipelineDeleter> attachCursorSourceToPipelineForLocalRead(
        const boost::intrusive_ptr<ExpressionContext>& expCtx, Pipeline* ownedPipeline) final;

    boost::optional<Document> lookupSingleDocument(
        const boost::intrusive_ptr<ExpressionContext>& expCtx,
        const NamespaceString& nss,
        UUID collectionUUID,
        const Document& documentKey,
        boost::optional<BSONObj> readConcern,
        bool allowSpeculativeMajorityRead);

    std::unique_ptr<ShardFilterer> getShardFilterer(
        const boost::intrusive_ptr<ExpressionContext>& expCtx) const override {
        // Try to emulate the behavior mongers and mongerd would each follow.
        if (expCtx->inMongers) {
            return nullptr;
        } else {
            return std::make_unique<StubShardFilterer>();
        }
    }

private:
    std::deque<DocumentSource::GetNextResult> _mockResults;
};
}  // namespace monger
