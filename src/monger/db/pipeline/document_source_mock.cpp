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

#include "monger/db/pipeline/document_source_mock.h"

#include "monger/db/pipeline/document.h"
#include "monger/db/pipeline/expression_context.h"
#include "monger/db/pipeline/expression_context_for_test.h"

namespace monger {

using boost::intrusive_ptr;
using std::deque;

DocumentSourceMock::DocumentSourceMock(deque<GetNextResult> results)
    : DocumentSourceQueue(std::move(results), new ExpressionContextForTest()) {}

const char* DocumentSourceMock::getSourceName() const {
    return "mock";
}

intrusive_ptr<DocumentSourceMock> DocumentSourceMock::createForTest(Document doc) {
    return new DocumentSourceMock({std::move(doc)});
}

intrusive_ptr<DocumentSourceMock> DocumentSourceMock::createForTest(deque<GetNextResult> results) {
    return new DocumentSourceMock(std::move(results));
}

intrusive_ptr<DocumentSourceMock> DocumentSourceMock::createForTest() {
    return new DocumentSourceMock(deque<GetNextResult>());
}

intrusive_ptr<DocumentSourceMock> DocumentSourceMock::createForTest(const GetNextResult& result) {
    deque<GetNextResult> results = {result};
    return new DocumentSourceMock(std::move(results));
}

intrusive_ptr<DocumentSourceMock> DocumentSourceMock::createForTest(const char* json) {
    return createForTest(Document(fromjson(json)));
}

intrusive_ptr<DocumentSourceMock> DocumentSourceMock::createForTest(
    const std::initializer_list<const char*>& jsons) {
    deque<GetNextResult> results;
    for (auto&& json : jsons) {
        results.emplace_back(Document(fromjson(json)));
    }
    return new DocumentSourceMock(std::move(results));
}
}
