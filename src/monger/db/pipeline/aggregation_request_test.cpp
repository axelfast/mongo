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

#include "monger/db/pipeline/aggregation_request.h"

#include "monger/bson/bsonobj.h"
#include "monger/bson/bsonobjbuilder.h"
#include "monger/bson/json.h"
#include "monger/db/catalog/document_validation.h"
#include "monger/db/namespace_string.h"
#include "monger/db/pipeline/document.h"
#include "monger/db/pipeline/document_value_test_util.h"
#include "monger/db/pipeline/value.h"
#include "monger/db/query/query_request.h"
#include "monger/db/repl/read_concern_args.h"
#include "monger/unittest/unittest.h"
#include "monger/util/assert_util.h"

namespace monger {
namespace {

const Document kDefaultCursorOptionDocument{
    {AggregationRequest::kBatchSizeName, AggregationRequest::kDefaultBatchSize}};

//
// Parsing
//

TEST(AggregationRequestTest, ShouldParseAllKnownOptions) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson(
        "{pipeline: [{$match: {a: 'abc'}}], explain: false, allowDiskUse: true, fromMongers: true, "
        "needsMerge: true, bypassDocumentValidation: true, collation: {locale: 'en_US'}, cursor: "
        "{batchSize: 10}, hint: {a: 1}, maxTimeMS: 100, readConcern: {level: 'linearizable'}, "
        "$queryOptions: {$readPreference: 'nearest'}, comment: 'agg_comment', exchange: {policy: "
        "'roundrobin', consumers:NumberInt(2)}}");
    auto request = unittest::assertGet(AggregationRequest::parseFromBSON(nss, inputBson));
    ASSERT_FALSE(request.getExplain());
    ASSERT_TRUE(request.shouldAllowDiskUse());
    ASSERT_TRUE(request.isFromMongers());
    ASSERT_TRUE(request.needsMerge());
    ASSERT_TRUE(request.shouldBypassDocumentValidation());
    ASSERT_EQ(request.getBatchSize(), 10);
    ASSERT_BSONOBJ_EQ(request.getHint(), BSON("a" << 1));
    ASSERT_EQ(request.getComment(), "agg_comment");
    ASSERT_BSONOBJ_EQ(request.getCollation(),
                      BSON("locale"
                           << "en_US"));
    ASSERT_EQ(request.getMaxTimeMS(), 100u);
    ASSERT_BSONOBJ_EQ(request.getReadConcern(),
                      BSON("level"
                           << "linearizable"));
    ASSERT_BSONOBJ_EQ(request.getUnwrappedReadPref(),
                      BSON("$readPreference"
                           << "nearest"));
    ASSERT_TRUE(request.getExchangeSpec().is_initialized());
}

TEST(AggregationRequestTest, ShouldParseExplicitExplainTrue) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [], explain: true, cursor: {}}");
    auto request = unittest::assertGet(AggregationRequest::parseFromBSON(nss, inputBson));
    ASSERT_TRUE(request.getExplain());
    ASSERT(*request.getExplain() == ExplainOptions::Verbosity::kQueryPlanner);
}

TEST(AggregationRequestTest, ShouldParseExplicitExplainFalseWithCursorOption) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [], explain: false, cursor: {batchSize: 10}}");
    auto request = unittest::assertGet(AggregationRequest::parseFromBSON(nss, inputBson));
    ASSERT_FALSE(request.getExplain());
    ASSERT_EQ(request.getBatchSize(), 10);
}

TEST(AggregationRequestTest, ShouldParseWithSeparateQueryPlannerExplainModeArg) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [], cursor: {}}");
    auto request = unittest::assertGet(AggregationRequest::parseFromBSON(
        nss, inputBson, ExplainOptions::Verbosity::kQueryPlanner));
    ASSERT_TRUE(request.getExplain());
    ASSERT(*request.getExplain() == ExplainOptions::Verbosity::kQueryPlanner);
}

TEST(AggregationRequestTest, ShouldParseWithSeparateQueryPlannerExplainModeArgAndCursorOption) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [], cursor: {batchSize: 10}}");
    auto request = unittest::assertGet(
        AggregationRequest::parseFromBSON(nss, inputBson, ExplainOptions::Verbosity::kExecStats));
    ASSERT_TRUE(request.getExplain());
    ASSERT(*request.getExplain() == ExplainOptions::Verbosity::kExecStats);
    ASSERT_EQ(request.getBatchSize(), 10);
}

TEST(AggregationRequestTest, ShouldParseExplainFlagWithReadConcern) {
    NamespaceString nss("a.collection");
    // Non-local readConcern should not be allowed with the explain flag, but this is checked
    // elsewhere to avoid having to parse the readConcern in AggregationRequest.
    const BSONObj inputBson =
        fromjson("{pipeline: [], explain: true, readConcern: {level: 'majority'}}");
    auto request = unittest::assertGet(AggregationRequest::parseFromBSON(nss, inputBson));
    ASSERT_TRUE(request.getExplain());
    ASSERT_BSONOBJ_EQ(request.getReadConcern(),
                      BSON("level"
                           << "majority"));
}

//
// Serialization
//

TEST(AggregationRequestTest, ShouldOnlySerializeRequiredFieldsIfNoOptionalFieldsAreSpecified) {
    NamespaceString nss("a.collection");
    AggregationRequest request(nss, {});

    auto expectedSerialization =
        Document{{AggregationRequest::kCommandName, nss.coll()},
                 {AggregationRequest::kPipelineName, Value(std::vector<Value>{})},
                 {AggregationRequest::kCursorName, Value(kDefaultCursorOptionDocument)}};
    ASSERT_DOCUMENT_EQ(request.serializeToCommandObj(), expectedSerialization);
}

TEST(AggregationRequestTest, ShouldNotSerializeOptionalValuesIfEquivalentToDefault) {
    NamespaceString nss("a.collection");
    AggregationRequest request(nss, {});
    request.setExplain(boost::none);
    request.setAllowDiskUse(false);
    request.setFromMongers(false);
    request.setNeedsMerge(false);
    request.setBypassDocumentValidation(false);
    request.setCollation(BSONObj());
    request.setHint(BSONObj());
    request.setComment("");
    request.setMaxTimeMS(0u);
    request.setUnwrappedReadPref(BSONObj());
    request.setReadConcern(BSONObj());

    auto expectedSerialization =
        Document{{AggregationRequest::kCommandName, nss.coll()},
                 {AggregationRequest::kPipelineName, Value(std::vector<Value>{})},
                 {AggregationRequest::kCursorName, Value(kDefaultCursorOptionDocument)}};
    ASSERT_DOCUMENT_EQ(request.serializeToCommandObj(), expectedSerialization);
}

TEST(AggregationRequestTest, ShouldSerializeOptionalValuesIfSet) {
    NamespaceString nss("a.collection");
    AggregationRequest request(nss, {});
    request.setAllowDiskUse(true);
    request.setFromMongers(true);
    request.setNeedsMerge(true);
    request.setBypassDocumentValidation(true);
    request.setBatchSize(10);
    request.setMaxTimeMS(10u);
    const auto hintObj = BSON("a" << 1);
    request.setHint(hintObj);
    const auto comment = std::string("agg_comment");
    request.setComment(comment);
    const auto collationObj = BSON("locale"
                                   << "en_US");
    request.setCollation(collationObj);
    const auto readPrefObj = BSON("$readPreference"
                                  << "nearest");
    request.setUnwrappedReadPref(readPrefObj);
    const auto readConcernObj = BSON("level"
                                     << "linearizable");
    request.setReadConcern(readConcernObj);

    auto expectedSerialization =
        Document{{AggregationRequest::kCommandName, nss.coll()},
                 {AggregationRequest::kPipelineName, Value(std::vector<Value>{})},
                 {AggregationRequest::kAllowDiskUseName, true},
                 {AggregationRequest::kFromMongersName, true},
                 {AggregationRequest::kNeedsMergeName, true},
                 {bypassDocumentValidationCommandOption(), true},
                 {AggregationRequest::kCollationName, collationObj},
                 {AggregationRequest::kCursorName,
                  Value(Document({{AggregationRequest::kBatchSizeName, 10}}))},
                 {AggregationRequest::kHintName, hintObj},
                 {AggregationRequest::kCommentName, comment},
                 {repl::ReadConcernArgs::kReadConcernFieldName, readConcernObj},
                 {QueryRequest::kUnwrappedReadPrefField, readPrefObj},
                 {QueryRequest::cmdOptionMaxTimeMS, 10}};
    ASSERT_DOCUMENT_EQ(request.serializeToCommandObj(), expectedSerialization);
}

TEST(AggregationRequestTest, ShouldSerializeBatchSizeIfSetAndExplainFalse) {
    NamespaceString nss("a.collection");
    AggregationRequest request(nss, {});
    request.setBatchSize(10);

    auto expectedSerialization =
        Document{{AggregationRequest::kCommandName, nss.coll()},
                 {AggregationRequest::kPipelineName, Value(std::vector<Value>{})},
                 {AggregationRequest::kCursorName,
                  Value(Document({{AggregationRequest::kBatchSizeName, 10}}))}};
    ASSERT_DOCUMENT_EQ(request.serializeToCommandObj(), expectedSerialization);
}

TEST(AggregationRequestTest, ShouldSerialiseAggregateFieldToOneIfCollectionIsAggregateOneNSS) {
    NamespaceString nss = NamespaceString::makeCollectionlessAggregateNSS("a");
    AggregationRequest request(nss, {});

    auto expectedSerialization =
        Document{{AggregationRequest::kCommandName, 1},
                 {AggregationRequest::kPipelineName, Value(std::vector<Value>{})},
                 {AggregationRequest::kCursorName,
                  Value(Document({{AggregationRequest::kBatchSizeName,
                                   AggregationRequest::kDefaultBatchSize}}))}};

    ASSERT_DOCUMENT_EQ(request.serializeToCommandObj(), expectedSerialization);
}

TEST(AggregationRequestTest, ShouldSetBatchSizeToDefaultOnEmptyCursorObject) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}}");
    auto request = AggregationRequest::parseFromBSON(nss, inputBson);
    ASSERT_OK(request.getStatus());
    ASSERT_EQ(request.getValue().getBatchSize(), AggregationRequest::kDefaultBatchSize);
}

TEST(AggregationRequestTest, ShouldAcceptHintAsString) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], hint: 'a_1', cursor: {}}");
    auto request = AggregationRequest::parseFromBSON(nss, inputBson);
    ASSERT_OK(request.getStatus());
    ASSERT_BSONOBJ_EQ(request.getValue().getHint(),
                      BSON("$hint"
                           << "a_1"));
}

TEST(AggregationRequestTest, ShouldNotSerializeBatchSizeWhenExplainSet) {
    NamespaceString nss("a.collection");
    AggregationRequest request(nss, {});
    request.setBatchSize(10);
    request.setExplain(ExplainOptions::Verbosity::kQueryPlanner);

    auto expectedSerialization =
        Document{{AggregationRequest::kCommandName, nss.coll()},
                 {AggregationRequest::kPipelineName, Value(std::vector<Value>{})},
                 {AggregationRequest::kCursorName, Value(Document())}};
    ASSERT_DOCUMENT_EQ(request.serializeToCommandObj(), expectedSerialization);
}

//
// Error cases.
//

TEST(AggregationRequestTest, ShouldRejectNonArrayPipeline) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: {}, cursor: {}}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectPipelineArrayIfAnElementIsNotAnObject) {
    NamespaceString nss("a.collection");
    BSONObj inputBson = fromjson("{pipeline: [4], cursor: {}}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());

    inputBson = fromjson("{pipeline: [{$match: {a: 'abc'}}, 4], cursor: {}}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonObjectCollation) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, collation: 1}");
    ASSERT_NOT_OK(
        AggregationRequest::parseFromBSON(NamespaceString("a.collection"), inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonStringNonObjectHint) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, hint: 1}");
    ASSERT_NOT_OK(
        AggregationRequest::parseFromBSON(NamespaceString("a.collection"), inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectHintAsArray) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, hint: []}]}");
    ASSERT_NOT_OK(
        AggregationRequest::parseFromBSON(NamespaceString("a.collection"), inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonStringComment) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, comment: 1}");
    ASSERT_NOT_OK(
        AggregationRequest::parseFromBSON(NamespaceString("a.collection"), inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectExplainIfNumber) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, explain: 1}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectExplainIfObject) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, explain: {}}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonBoolFromMongers) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, fromMongers: 1}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonBoolNeedsMerge) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, needsMerge: 1, fromMongers: true}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNeedsMergeIfFromMongersNotPresent) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, needsMerge: true}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonBoolNeedsMerge34) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, fromRouter: 1}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNeedsMergeIfNeedsMerge34AlsoPresent) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson(
        "{pipeline: [{$match: {a: 'abc'}}], cursor: {}, needsMerge: true, fromMongers: true, "
        "fromRouter: true}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectFromMongersIfNeedsMerge34AlsoPresent) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson(
        "{pipeline: [{$match: {a: 'abc'}}], cursor: {}, fromMongers: true, fromRouter: true}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonBoolAllowDiskUse) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, allowDiskUse: 1}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNoCursorNoExplain) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [{$match: {a: 'abc'}}]}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectExplainTrueWithSeparateExplainArg) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [], explain: true}");
    ASSERT_NOT_OK(
        AggregationRequest::parseFromBSON(nss, inputBson, ExplainOptions::Verbosity::kExecStats)
            .getStatus());
}

TEST(AggregationRequestTest, ShouldRejectExplainFalseWithSeparateExplainArg) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [], explain: false}");
    ASSERT_NOT_OK(
        AggregationRequest::parseFromBSON(nss, inputBson, ExplainOptions::Verbosity::kExecStats)
            .getStatus());
}

TEST(AggregationRequestTest, ShouldRejectExplainExecStatsVerbosityWithReadConcernMajority) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [], readConcern: {level: 'majority'}}");
    ASSERT_NOT_OK(
        AggregationRequest::parseFromBSON(nss, inputBson, ExplainOptions::Verbosity::kExecStats)
            .getStatus());
}

TEST(AggregationRequestTest, ShouldRejectExplainWithWriteConcernMajority) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [], explain: true, writeConcern: {w: 'majority'}}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectExplainExecStatsVerbosityWithWriteConcernMajority) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [], writeConcern: {w: 'majority'}}");
    ASSERT_NOT_OK(
        AggregationRequest::parseFromBSON(nss, inputBson, ExplainOptions::Verbosity::kExecStats)
            .getStatus());
}

TEST(AggregationRequestTest, CannotParseNeedsMerge34) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, fromRouter: true}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ParseNSShouldReturnAggregateOneNSIfAggregateFieldIsOne) {
    const std::vector<std::string> ones{
        "1", "1.0", "NumberInt(1)", "NumberLong(1)", "NumberDecimal('1')"};

    for (auto& one : ones) {
        const BSONObj inputBSON =
            fromjson(str::stream() << "{aggregate: " << one << ", pipeline: []}");
        ASSERT(AggregationRequest::parseNs("a", inputBSON).isCollectionlessAggregateNS());
    }
}

TEST(AggregationRequestTest, ParseNSShouldRejectNumericNSIfAggregateFieldIsNotOne) {
    const BSONObj inputBSON = fromjson("{aggregate: 2, pipeline: []}");
    ASSERT_THROWS_CODE(
        AggregationRequest::parseNs("a", inputBSON), AssertionException, ErrorCodes::FailedToParse);
}

TEST(AggregationRequestTest, ParseNSShouldRejectNonStringNonNumericNS) {
    const BSONObj inputBSON = fromjson("{aggregate: {}, pipeline: []}");
    ASSERT_THROWS_CODE(
        AggregationRequest::parseNs("a", inputBSON), AssertionException, ErrorCodes::TypeMismatch);
}

TEST(AggregationRequestTest, ParseNSShouldRejectAggregateOneStringAsCollectionName) {
    const BSONObj inputBSON = fromjson("{aggregate: '$cmd.aggregate', pipeline: []}");
    ASSERT_THROWS_CODE(AggregationRequest::parseNs("a", inputBSON),
                       AssertionException,
                       ErrorCodes::InvalidNamespace);
}

TEST(AggregationRequestTest, ParseNSShouldRejectInvalidCollectionName) {
    const BSONObj inputBSON = fromjson("{aggregate: '', pipeline: []}");
    ASSERT_THROWS_CODE(AggregationRequest::parseNs("a", inputBSON),
                       AssertionException,
                       ErrorCodes::InvalidNamespace);
}

TEST(AggregationRequestTest, ParseFromBSONOverloadsShouldProduceIdenticalRequests) {
    const BSONObj inputBSON =
        fromjson("{aggregate: 'collection', pipeline: [{$match: {}}, {$project: {}}], cursor: {}}");
    NamespaceString nss("a.collection");

    auto aggReqDBName = unittest::assertGet(AggregationRequest::parseFromBSON("a", inputBSON));
    auto aggReqNSS = unittest::assertGet(AggregationRequest::parseFromBSON(nss, inputBSON));

    ASSERT_DOCUMENT_EQ(aggReqDBName.serializeToCommandObj(), aggReqNSS.serializeToCommandObj());
}

TEST(AggregationRequestTest, ShouldRejectExchangeNotObject) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [], exchage: '42'}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectExchangeInvalidSpec) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [], exchage: {}}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectInvalidWriteConcern) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, writeConcern: 'invalid'}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}
//
// Ignore fields parsed elsewhere.
//

TEST(AggregationRequestTest, ShouldIgnoreQueryOptions) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, $queryOptions: {}}");
    ASSERT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

}  // namespace
}  // namespace monger
