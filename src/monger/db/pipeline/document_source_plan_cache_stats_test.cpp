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

#include <algorithm>

#include "monger/bson/json.h"
#include "monger/db/pipeline/aggregation_context_fixture.h"
#include "monger/db/pipeline/document_source_plan_cache_stats.h"
#include "monger/db/pipeline/stub_monger_process_interface.h"
#include "monger/unittest/unittest.h"

namespace monger {

using DocumentSourcePlanCacheStatsTest = AggregationContextFixture;

/**
 * A MongerProcessInterface used for testing which returns artificial plan cache stats.
 */
class PlanCacheStatsMongerProcessInterface final : public StubMongerProcessInterface {
public:
    PlanCacheStatsMongerProcessInterface(std::vector<BSONObj> planCacheStats)
        : _planCacheStats(std::move(planCacheStats)) {}

    std::vector<BSONObj> getMatchingPlanCacheEntryStats(
        OperationContext* opCtx,
        const NamespaceString& nss,
        const MatchExpression* matchExpr) const override {
        std::vector<BSONObj> filteredStats{};
        std::copy_if(_planCacheStats.begin(),
                     _planCacheStats.end(),
                     std::back_inserter(filteredStats),
                     [&matchExpr](const BSONObj& obj) { return matchExpr->matchesBSON(obj); });
        return filteredStats;
    }

private:
    std::vector<BSONObj> _planCacheStats;
};

TEST_F(DocumentSourcePlanCacheStatsTest, ShouldFailToParseIfSpecIsNotObject) {
    const auto specObj = fromjson("{$planCacheStats: 1}");
    ASSERT_THROWS_CODE(
        DocumentSourcePlanCacheStats::createFromBson(specObj.firstElement(), getExpCtx()),
        AssertionException,
        ErrorCodes::FailedToParse);
}

TEST_F(DocumentSourcePlanCacheStatsTest, ShouldFailToParseIfSpecIsANonEmptyObject) {
    const auto specObj = fromjson("{$planCacheStats: {unknownOption: 1}}");
    ASSERT_THROWS_CODE(
        DocumentSourcePlanCacheStats::createFromBson(specObj.firstElement(), getExpCtx()),
        AssertionException,
        ErrorCodes::FailedToParse);
}

TEST_F(DocumentSourcePlanCacheStatsTest, CannotCreateWhenInMongers) {
    const auto specObj = fromjson("{$planCacheStats: {}}");
    getExpCtx()->inMongers = true;
    ASSERT_THROWS_CODE(
        DocumentSourcePlanCacheStats::createFromBson(specObj.firstElement(), getExpCtx()),
        AssertionException,
        50932);
}

TEST_F(DocumentSourcePlanCacheStatsTest, CanParseAndSerializeSuccessfully) {
    const auto specObj = fromjson("{$planCacheStats: {}}");
    auto stage = DocumentSourcePlanCacheStats::createFromBson(specObj.firstElement(), getExpCtx());
    std::vector<Value> serialized;
    stage->serializeToArray(serialized);
    ASSERT_EQ(1u, serialized.size());
    ASSERT_BSONOBJ_EQ(specObj, serialized[0].getDocument().toBson());
}

TEST_F(DocumentSourcePlanCacheStatsTest, CanParseAndSerializeAsExplainSuccessfully) {
    const auto specObj = fromjson("{$planCacheStats: {}}");
    auto stage = DocumentSourcePlanCacheStats::createFromBson(specObj.firstElement(), getExpCtx());
    std::vector<Value> serialized;
    stage->serializeToArray(serialized, ExplainOptions::Verbosity::kQueryPlanner);
    ASSERT_EQ(1u, serialized.size());
    ASSERT_BSONOBJ_EQ(specObj, serialized[0].getDocument().toBson());
}

TEST_F(DocumentSourcePlanCacheStatsTest, SerializesSuccessfullyAfterAbsorbingMatch) {
    const auto specObj = fromjson("{$planCacheStats: {}}");
    auto planCacheStats =
        DocumentSourcePlanCacheStats::createFromBson(specObj.firstElement(), getExpCtx());
    auto match = DocumentSourceMatch::create(fromjson("{foo: 'bar'}"), getExpCtx());
    auto pipeline = unittest::assertGet(Pipeline::create({planCacheStats, match}, getExpCtx()));
    ASSERT_EQ(2u, pipeline->getSources().size());

    pipeline->optimizePipeline();
    ASSERT_EQ(1u, pipeline->getSources().size());

    auto serialized = pipeline->serialize();
    ASSERT_EQ(2u, serialized.size());
    ASSERT_BSONOBJ_EQ(specObj, serialized[0].getDocument().toBson());
    ASSERT_BSONOBJ_EQ(fromjson("{$match: {foo: 'bar'}}"), serialized[1].getDocument().toBson());
}

TEST_F(DocumentSourcePlanCacheStatsTest, SerializesSuccessfullyAfterAbsorbingMatchForExplain) {
    const auto specObj = fromjson("{$planCacheStats: {}}");
    auto planCacheStats =
        DocumentSourcePlanCacheStats::createFromBson(specObj.firstElement(), getExpCtx());
    auto match = DocumentSourceMatch::create(fromjson("{foo: 'bar'}"), getExpCtx());
    auto pipeline = unittest::assertGet(Pipeline::create({planCacheStats, match}, getExpCtx()));
    ASSERT_EQ(2u, pipeline->getSources().size());

    pipeline->optimizePipeline();
    ASSERT_EQ(1u, pipeline->getSources().size());

    auto serialized = pipeline->writeExplainOps(ExplainOptions::Verbosity::kQueryPlanner);
    ASSERT_EQ(1u, serialized.size());
    ASSERT_BSONOBJ_EQ(fromjson("{$planCacheStats: {match: {foo: 'bar'}}}"),
                      serialized[0].getDocument().toBson());
}

TEST_F(DocumentSourcePlanCacheStatsTest, ReturnsImmediateEOFWithEmptyPlanCache) {
    getExpCtx()->mongerProcessInterface =
        std::make_shared<PlanCacheStatsMongerProcessInterface>(std::vector<BSONObj>{});
    const auto specObj = fromjson("{$planCacheStats: {}}");
    auto stage = DocumentSourcePlanCacheStats::createFromBson(specObj.firstElement(), getExpCtx());
    ASSERT(stage->getNext().isEOF());
    ASSERT(stage->getNext().isEOF());
}

TEST_F(DocumentSourcePlanCacheStatsTest, ReturnsOnlyMatchingStatsAfterAbsorbingMatch) {
    std::vector<BSONObj> stats{BSONObj(),
                               BSON("foo"
                                    << "bar"),
                               BSON("foo"
                                    << "baz"),
                               BSON("foo"
                                    << "bar"
                                    << "match"
                                    << true)};
    getExpCtx()->mongerProcessInterface =
        std::make_shared<PlanCacheStatsMongerProcessInterface>(stats);

    const auto specObj = fromjson("{$planCacheStats: {}}");
    auto planCacheStats =
        DocumentSourcePlanCacheStats::createFromBson(specObj.firstElement(), getExpCtx());
    auto match = DocumentSourceMatch::create(fromjson("{foo: 'bar'}"), getExpCtx());
    auto pipeline = unittest::assertGet(Pipeline::create({planCacheStats, match}, getExpCtx()));
    pipeline->optimizePipeline();

    ASSERT_BSONOBJ_EQ(pipeline->getNext()->toBson(), stats[1]);
    ASSERT_BSONOBJ_EQ(pipeline->getNext()->toBson(), stats[3]);
    ASSERT(!pipeline->getNext());
}

}  // namespace monger
