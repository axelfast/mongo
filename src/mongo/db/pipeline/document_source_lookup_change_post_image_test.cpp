/**
 *    Copyright (C) 2018-present MongerDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongerDB, Inc.
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

#include <boost/intrusive_ptr.hpp>
#include <deque>
#include <vector>

#include "monger/bson/bsonmisc.h"
#include "monger/bson/bsonobj.h"
#include "monger/bson/bsonobjbuilder.h"
#include "monger/db/pipeline/aggregation_context_fixture.h"
#include "monger/db/pipeline/document.h"
#include "monger/db/pipeline/document_source_change_stream.h"
#include "monger/db/pipeline/document_source_lookup_change_post_image.h"
#include "monger/db/pipeline/document_source_mock.h"
#include "monger/db/pipeline/document_value_test_util.h"
#include "monger/db/pipeline/field_path.h"
#include "monger/db/pipeline/stub_monger_process_interface_lookup_single_document.h"
#include "monger/db/pipeline/value.h"

namespace monger {
namespace {
using boost::intrusive_ptr;
using std::deque;
using std::vector;

using MockMongerInterface = StubMongerProcessInterfaceLookupSingleDocument;

// This provides access to getExpCtx(), but we'll use a different name for this test suite.
class DocumentSourceLookupChangePostImageTest : public AggregationContextFixture {
public:
    /**
     * This method is required to avoid a static initialization fiasco resulting from calling
     * UUID::gen() in file static scope.
     */
    static const UUID& testUuid() {
        static const UUID* uuid_gen = new UUID(UUID::gen());
        return *uuid_gen;
    }

    Document makeResumeToken(ImplicitValue id = Value()) {
        const Timestamp ts(100, 1);
        if (id.missing()) {
            ResumeTokenData tokenData;
            tokenData.clusterTime = ts;
            return ResumeToken(tokenData).toDocument();
        }
        return ResumeToken(ResumeTokenData(ts, 0, 0, testUuid(), Value(Document{{"_id", id}})))
            .toDocument();
    }
};

TEST_F(DocumentSourceLookupChangePostImageTest, ShouldErrorIfMissingDocumentKeyOnUpdate) {
    auto expCtx = getExpCtx();

    // Set up the lookup change post image stage.
    auto lookupChangeStage = DocumentSourceLookupChangePostImage::create(expCtx);

    // Mock its input with a document without a "documentKey" field.
    auto mockLocalSource = DocumentSourceMock::createForTest(
        Document{{"_id", makeResumeToken(0)},
                 {"operationType", "update"_sd},
                 {"fullDocument", Document{{"_id", 0}}},
                 {"ns", Document{{"db", expCtx->ns.db()}, {"coll", expCtx->ns.coll()}}}});

    lookupChangeStage->setSource(mockLocalSource.get());

    // Mock out the foreign collection.
    getExpCtx()->mongerProcessInterface =
        std::make_unique<MockMongerInterface>(deque<DocumentSource::GetNextResult>{});

    ASSERT_THROWS_CODE(lookupChangeStage->getNext(), AssertionException, 40578);
}

TEST_F(DocumentSourceLookupChangePostImageTest, ShouldErrorIfMissingOperationType) {
    auto expCtx = getExpCtx();

    // Set up the lookup change post image stage.
    auto lookupChangeStage = DocumentSourceLookupChangePostImage::create(expCtx);

    // Mock its input with a document without a "ns" field.
    auto mockLocalSource = DocumentSourceMock::createForTest(
        Document{{"_id", makeResumeToken(0)},
                 {"documentKey", Document{{"_id", 0}}},
                 {"fullDocument", Document{{"_id", 0}}},
                 {"ns", Document{{"db", expCtx->ns.db()}, {"coll", expCtx->ns.coll()}}}});

    lookupChangeStage->setSource(mockLocalSource.get());

    // Mock out the foreign collection.
    getExpCtx()->mongerProcessInterface =
        std::make_unique<MockMongerInterface>(deque<DocumentSource::GetNextResult>{});

    ASSERT_THROWS_CODE(lookupChangeStage->getNext(), AssertionException, 40578);
}

TEST_F(DocumentSourceLookupChangePostImageTest, ShouldErrorIfMissingNamespace) {
    auto expCtx = getExpCtx();

    // Set up the lookup change post image stage.
    auto lookupChangeStage = DocumentSourceLookupChangePostImage::create(expCtx);

    // Mock its input with a document without a "ns" field.
    auto mockLocalSource = DocumentSourceMock::createForTest(Document{
        {"_id", makeResumeToken(0)},
        {"documentKey", Document{{"_id", 0}}},
        {"operationType", "update"_sd},
    });

    lookupChangeStage->setSource(mockLocalSource.get());

    // Mock out the foreign collection.
    getExpCtx()->mongerProcessInterface =
        std::make_unique<MockMongerInterface>(deque<DocumentSource::GetNextResult>{});

    ASSERT_THROWS_CODE(lookupChangeStage->getNext(), AssertionException, 40578);
}

TEST_F(DocumentSourceLookupChangePostImageTest, ShouldErrorIfNsFieldHasWrongType) {
    auto expCtx = getExpCtx();

    // Set up the lookup change post image stage.
    auto lookupChangeStage = DocumentSourceLookupChangePostImage::create(expCtx);

    // Mock its input with a document without a "ns" field.
    auto mockLocalSource =
        DocumentSourceMock::createForTest(Document{{"_id", makeResumeToken(0)},
                                                   {"documentKey", Document{{"_id", 0}}},
                                                   {"operationType", "update"_sd},
                                                   {"ns", 4}});

    lookupChangeStage->setSource(mockLocalSource.get());

    // Mock out the foreign collection.
    getExpCtx()->mongerProcessInterface =
        std::make_unique<MockMongerInterface>(deque<DocumentSource::GetNextResult>{});

    ASSERT_THROWS_CODE(lookupChangeStage->getNext(), AssertionException, 40578);
}

TEST_F(DocumentSourceLookupChangePostImageTest, ShouldErrorIfNsFieldDoesNotMatchPipeline) {
    auto expCtx = getExpCtx();

    // Set up the lookup change post image stage.
    auto lookupChangeStage = DocumentSourceLookupChangePostImage::create(expCtx);

    // Mock its input with a document without a "ns" field.
    auto mockLocalSource = DocumentSourceMock::createForTest(
        Document{{"_id", makeResumeToken(0)},
                 {"documentKey", Document{{"_id", 0}}},
                 {"operationType", "update"_sd},
                 {"ns", Document{{"db", "DIFFERENT"_sd}, {"coll", expCtx->ns.coll()}}}});

    lookupChangeStage->setSource(mockLocalSource.get());

    // Mock out the foreign collection.
    getExpCtx()->mongerProcessInterface =
        std::make_unique<MockMongerInterface>(deque<DocumentSource::GetNextResult>{});

    ASSERT_THROWS_CODE(lookupChangeStage->getNext(), AssertionException, 40579);
}

TEST_F(DocumentSourceLookupChangePostImageTest, ShouldErrorIfDatabaseMismatchOnCollectionlessNss) {
    auto expCtx = getExpCtx();

    expCtx->ns = NamespaceString::makeCollectionlessAggregateNSS("test");

    // Set up the lookup change post image stage.
    auto lookupChangeStage = DocumentSourceLookupChangePostImage::create(expCtx);

    // Mock its input with a document without a "ns" field.
    auto mockLocalSource = DocumentSourceMock::createForTest(
        Document{{"_id", makeResumeToken(0)},
                 {"documentKey", Document{{"_id", 0}}},
                 {"operationType", "update"_sd},
                 {"ns", Document{{"db", "DIFFERENT"_sd}, {"coll", "irrelevant"_sd}}}});

    lookupChangeStage->setSource(mockLocalSource.get());

    // Mock out the foreign collection.
    deque<DocumentSource::GetNextResult> mockForeignContents{Document{{"_id", 0}}};
    expCtx->mongerProcessInterface = std::make_unique<MockMongerInterface>(mockForeignContents);

    ASSERT_THROWS_CODE(lookupChangeStage->getNext(), AssertionException, 40579);
}

TEST_F(DocumentSourceLookupChangePostImageTest, ShouldPassIfDatabaseMatchesOnCollectionlessNss) {
    auto expCtx = getExpCtx();

    expCtx->ns = NamespaceString::makeCollectionlessAggregateNSS("test");

    // Set up the lookup change post image stage.
    auto lookupChangeStage = DocumentSourceLookupChangePostImage::create(expCtx);

    // Mock out the foreign collection.
    deque<DocumentSource::GetNextResult> mockForeignContents{Document{{"_id", 0}}};
    expCtx->mongerProcessInterface = std::make_unique<MockMongerInterface>(mockForeignContents);

    auto mockLocalSource = DocumentSourceMock::createForTest(
        Document{{"_id", makeResumeToken(0)},
                 {"documentKey", Document{{"_id", 0}}},
                 {"operationType", "update"_sd},
                 {"ns", Document{{"db", expCtx->ns.db()}, {"coll", "irrelevant"_sd}}}});

    lookupChangeStage->setSource(mockLocalSource.get());

    auto next = lookupChangeStage->getNext();
    ASSERT_TRUE(next.isAdvanced());
    ASSERT_DOCUMENT_EQ(
        next.releaseDocument(),
        (Document{{"_id", makeResumeToken(0)},
                  {"documentKey", Document{{"_id", 0}}},
                  {"operationType", "update"_sd},
                  {"ns", Document{{"db", expCtx->ns.db()}, {"coll", "irrelevant"_sd}}},
                  {"fullDocument", Document{{"_id", 0}}}}));
}

TEST_F(DocumentSourceLookupChangePostImageTest, ShouldErrorIfDocumentKeyIsNotUnique) {
    auto expCtx = getExpCtx();

    // Set up the lookup change post image stage.
    auto lookupChangeStage = DocumentSourceLookupChangePostImage::create(expCtx);

    // Mock its input with an update document.
    auto mockLocalSource = DocumentSourceMock::createForTest(
        Document{{"_id", makeResumeToken(0)},
                 {"documentKey", Document{{"_id", 0}}},
                 {"operationType", "update"_sd},
                 {"ns", Document{{"db", expCtx->ns.db()}, {"coll", expCtx->ns.coll()}}}});

    lookupChangeStage->setSource(mockLocalSource.get());

    // Mock out the foreign collection to have two documents with the same document key.
    deque<DocumentSource::GetNextResult> foreignCollection = {Document{{"_id", 0}},
                                                              Document{{"_id", 0}}};
    getExpCtx()->mongerProcessInterface =
        std::make_unique<MockMongerInterface>(std::move(foreignCollection));

    ASSERT_THROWS_CODE(
        lookupChangeStage->getNext(), AssertionException, ErrorCodes::TooManyMatchingDocuments);
}

TEST_F(DocumentSourceLookupChangePostImageTest, ShouldPropagatePauses) {
    auto expCtx = getExpCtx();

    // Set up the lookup change post image stage.
    auto lookupChangeStage = DocumentSourceLookupChangePostImage::create(expCtx);

    // Mock its input, pausing every other result.
    auto mockLocalSource = DocumentSourceMock::createForTest(
        {Document{{"_id", makeResumeToken(0)},
                  {"documentKey", Document{{"_id", 0}}},
                  {"operationType", "insert"_sd},
                  {"ns", Document{{"db", expCtx->ns.db()}, {"coll", expCtx->ns.coll()}}},
                  {"fullDocument", Document{{"_id", 0}}}},
         DocumentSource::GetNextResult::makePauseExecution(),
         Document{{"_id", makeResumeToken(1)},
                  {"documentKey", Document{{"_id", 1}}},
                  {"operationType", "update"_sd},
                  {"ns", Document{{"db", expCtx->ns.db()}, {"coll", expCtx->ns.coll()}}}},
         DocumentSource::GetNextResult::makePauseExecution()});

    lookupChangeStage->setSource(mockLocalSource.get());

    // Mock out the foreign collection.
    deque<DocumentSource::GetNextResult> mockForeignContents{Document{{"_id", 0}},
                                                             Document{{"_id", 1}}};
    getExpCtx()->mongerProcessInterface =
        std::make_unique<MockMongerInterface>(std::move(mockForeignContents));

    auto next = lookupChangeStage->getNext();
    ASSERT_TRUE(next.isAdvanced());
    ASSERT_DOCUMENT_EQ(
        next.releaseDocument(),
        (Document{{"_id", makeResumeToken(0)},
                  {"documentKey", Document{{"_id", 0}}},
                  {"operationType", "insert"_sd},
                  {"ns", Document{{"db", expCtx->ns.db()}, {"coll", expCtx->ns.coll()}}},
                  {"fullDocument", Document{{"_id", 0}}}}));

    ASSERT_TRUE(lookupChangeStage->getNext().isPaused());

    next = lookupChangeStage->getNext();
    ASSERT_TRUE(next.isAdvanced());
    ASSERT_DOCUMENT_EQ(
        next.releaseDocument(),
        (Document{{"_id", makeResumeToken(1)},
                  {"documentKey", Document{{"_id", 1}}},
                  {"operationType", "update"_sd},
                  {"ns", Document{{"db", expCtx->ns.db()}, {"coll", expCtx->ns.coll()}}},
                  {"fullDocument", Document{{"_id", 1}}}}));

    ASSERT_TRUE(lookupChangeStage->getNext().isPaused());

    ASSERT_TRUE(lookupChangeStage->getNext().isEOF());
    ASSERT_TRUE(lookupChangeStage->getNext().isEOF());
}

}  // namespace
}  // namespace monger
