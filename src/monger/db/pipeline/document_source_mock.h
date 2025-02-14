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

#include <deque>

#include "monger/db/pipeline/document_source_queue.h"

namespace monger {

/**
 * A mock DocumentSource which is useful for testing. In addition to re-spooling documents like
 * DocumentSourceQueue, it tracks some state about which methods have been called.
 */
class DocumentSourceMock : public DocumentSourceQueue {
public:
    // Constructors which create their own ExpressionContextForTest. Do _not_ use these outside of
    // tests, as they will spin up ServiceContexts (TODO SERVER-41060).
    static boost::intrusive_ptr<DocumentSourceMock> createForTest();

    static boost::intrusive_ptr<DocumentSourceMock> createForTest(Document doc);

    static boost::intrusive_ptr<DocumentSourceMock> createForTest(const GetNextResult& result);
    static boost::intrusive_ptr<DocumentSourceMock> createForTest(
        std::deque<GetNextResult> results);

    static boost::intrusive_ptr<DocumentSourceMock> createForTest(const char* json);
    static boost::intrusive_ptr<DocumentSourceMock> createForTest(
        const std::initializer_list<const char*>& jsons);

    using DocumentSourceQueue::DocumentSourceQueue;
    DocumentSourceMock(std::deque<GetNextResult>);

    GetNextResult getNext() override {
        invariant(!isDisposed);
        invariant(!isDetachedFromOpCtx);
        return DocumentSourceQueue::getNext();
    }
    Value serialize(
        boost::optional<ExplainOptions::Verbosity> explain = boost::none) const override {
        // Unlike the queue, it's okay to serialize this stage for testing purposes.
        return Value(Document{{getSourceName(), Document()}});
    }

    const char* getSourceName() const override;

    void reattachToOperationContext(OperationContext* opCtx) {
        isDetachedFromOpCtx = false;
    }

    void detachFromOperationContext() {
        isDetachedFromOpCtx = true;
    }

    boost::intrusive_ptr<DocumentSource> optimize() override {
        isOptimized = true;
        return this;
    }

    /**
     * This stage does not modify anything.
     */
    GetModPathsReturn getModifiedPaths() const override {
        return {GetModPathsReturn::Type::kFiniteSet, std::set<std::string>{}, {}};
    }

    boost::optional<DistributedPlanLogic> distributedPlanLogic() override {
        return boost::none;
    }

    bool isDisposed = false;
    bool isDetachedFromOpCtx = false;
    bool isOptimized = false;

protected:
    void doDispose() override {
        isDisposed = true;
    }
};

}  // namespace monger
