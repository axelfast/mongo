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

#include <memory>
#include <string>
#include <vector>

#include "monger/base/owned_pointer_vector.h"
#include "monger/base/status.h"
#include "monger/base/string_data.h"
#include "monger/db/exec/requires_all_indices_stage.h"
#include "monger/db/query/canonical_query.h"
#include "monger/db/query/plan_cache.h"
#include "monger/db/query/plan_yield_policy.h"
#include "monger/db/query/query_planner_params.h"
#include "monger/db/query/query_solution.h"
#include "monger/db/record_id.h"

namespace monger {

class OperationContext;

/**
 * The SubplanStage is used for rooted $or queries. It plans each clause of the $or
 * individually, and then creates an overall query plan based on the winning plan from
 * each clause.
 *
 * Uses the MultiPlanStage in order to rank plans for the individual clauses.
 *
 * Notes on caching strategy:
 *
 *   --Interaction with the plan cache is done on a per-clause basis. For a given clause C,
 *   if there is a plan in the cache for shape C, then C is planned using the index tags
 *   obtained from the plan cache entry. If no cached plan is found for C, then a MultiPlanStage
 *   is used to determine the best plan for the clause; unless there is a tie between multiple
 *   candidate plans, the winner is inserted into the plan cache and used to plan subsequent
 *   executions of C. These subsequent executions of shape C could be either as a clause in
 *   another rooted $or query, or shape C as its own query.
 *
 *   --Plans for entire rooted $or queries are neither written to nor read from the plan cache.
 */
class SubplanStage final : public RequiresAllIndicesStage {
public:
    SubplanStage(OperationContext* opCtx,
                 const Collection* collection,
                 WorkingSet* ws,
                 const QueryPlannerParams& params,
                 CanonicalQuery* cq);

    static bool canUseSubplanning(const CanonicalQuery& query);

    bool isEOF() final;
    StageState doWork(WorkingSetID* out) final;

    StageType stageType() const final {
        return STAGE_SUBPLAN;
    }

    std::unique_ptr<PlanStageStats> getStats();

    const SpecificStats* getSpecificStats() const final;

    static const char* kStageType;

    /**
     * Selects a plan using subplanning. First uses the query planning results from
     * planSubqueries() and the multi plan stage to select the best plan for each branch.
     *
     * If this effort fails, then falls back on planning the whole query normally rather
     * then planning $or branches independently.
     *
     * If 'yieldPolicy' is non-NULL, then all locks may be yielded in between round-robin
     * works of the candidate plans. By default, 'yieldPolicy' is NULL and no yielding will
     * take place.
     *
     * Returns a non-OK status if query planning fails. In particular, this function returns
     * ErrorCodes::QueryPlanKilled if the query plan was killed during a yield, or
     * ErrorCodes::MaxTimeMSExpired if the operation has exceeded its time limit.
     */
    Status pickBestPlan(PlanYieldPolicy* yieldPolicy);

    //
    // For testing.
    //

    /**
     * Returns true if the i-th branch was planned by retrieving a cached solution,
     * otherwise returns false.
     */
    bool branchPlannedFromCache(size_t i) const;

    /**
     * Provide access to the query solution for our composite solution. Does not relinquish
     * ownership.
     */
    QuerySolution* compositeSolution() const {
        return _compositeSolution.get();
    }

private:
    /**
     * A class used internally in order to keep track of the results of planning
     * a particular $or branch.
     */
    struct BranchPlanningResult {
        BranchPlanningResult(const BranchPlanningResult&) = delete;
        BranchPlanningResult& operator=(const BranchPlanningResult&) = delete;

    public:
        BranchPlanningResult() {}

        // A parsed version of one branch of the $or.
        std::unique_ptr<CanonicalQuery> canonicalQuery;

        // If there is cache data available, then we store it here rather than generating
        // a set of alternate plans for the branch. The index tags from the cache data
        // can be applied directly to the parent $or MatchExpression when generating the
        // composite solution.
        std::unique_ptr<CachedSolution> cachedSolution;

        // Query solutions resulting from planning the $or branch.
        std::vector<std::unique_ptr<QuerySolution>> solutions;
    };

    /**
     * Plan each branch of the $or independently, and store the resulting
     * lists of query solutions in '_solutions'.
     *
     * Called from SubplanStage::make so that construction of the subplan stage
     * fails immediately, rather than returning a plan executor and subsequently
     * through getNext(...).
     */
    Status planSubqueries();

    /**
     * Uses the query planning results from planSubqueries() and the multi plan stage
     * to select the best plan for each branch.
     *
     * Helper for pickBestPlan().
     */
    Status choosePlanForSubqueries(PlanYieldPolicy* yieldPolicy);

    /**
     * Used as a fallback if subplanning fails. Helper for pickBestPlan().
     */
    Status choosePlanWholeQuery(PlanYieldPolicy* yieldPolicy);

    // Not owned here.
    WorkingSet* _ws;

    QueryPlannerParams _plannerParams;

    // Not owned here.
    CanonicalQuery* _query;

    // The copy of the query that we will annotate with tags and use to construct the composite
    // solution. Must be a rooted $or query, or a contained $or that has been rewritten to a
    // rooted $or.
    std::unique_ptr<MatchExpression> _orExpression;

    // If we successfully create a "composite solution" by planning each $or branch
    // independently, that solution is owned here.
    std::unique_ptr<QuerySolution> _compositeSolution;

    // Holds a list of the results from planning each branch.
    std::vector<std::unique_ptr<BranchPlanningResult>> _branchResults;

    // We need this to extract cache-friendly index data from the index assignments.
    std::map<IndexEntry::Identifier, size_t> _indexMap;
};

}  // namespace monger
