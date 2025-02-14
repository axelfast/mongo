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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kQuery

#include "monger/platform/basic.h"

#include "monger/db/exec/cached_plan.h"

#include <memory>

#include "monger/db/catalog/collection.h"
#include "monger/db/concurrency/write_conflict_exception.h"
#include "monger/db/exec/multi_plan.h"
#include "monger/db/exec/scoped_timer.h"
#include "monger/db/exec/working_set_common.h"
#include "monger/db/query/explain.h"
#include "monger/db/query/plan_cache.h"
#include "monger/db/query/plan_ranker.h"
#include "monger/db/query/plan_yield_policy.h"
#include "monger/db/query/query_knobs_gen.h"
#include "monger/db/query/query_planner.h"
#include "monger/db/query/stage_builder.h"
#include "monger/util/log.h"
#include "monger/util/str.h"
#include "monger/util/transitional_tools_do_not_use/vector_spooling.h"

namespace monger {

// static
const char* CachedPlanStage::kStageType = "CACHED_PLAN";

CachedPlanStage::CachedPlanStage(OperationContext* opCtx,
                                 Collection* collection,
                                 WorkingSet* ws,
                                 CanonicalQuery* cq,
                                 const QueryPlannerParams& params,
                                 size_t decisionWorks,
                                 PlanStage* root)
    : RequiresAllIndicesStage(kStageType, opCtx, collection),
      _ws(ws),
      _canonicalQuery(cq),
      _plannerParams(params),
      _decisionWorks(decisionWorks) {
    _children.emplace_back(root);
}

Status CachedPlanStage::pickBestPlan(PlanYieldPolicy* yieldPolicy) {
    // Adds the amount of time taken by pickBestPlan() to executionTimeMillis. There's lots of
    // execution work that happens here, so this is needed for the time accounting to
    // make sense.
    ScopedTimer timer(getClock(), &_commonStats.executionTimeMillis);

    // During plan selection, the list of indices we are using to plan must remain stable, so the
    // query will die during yield recovery if any index has been dropped. However, once plan
    // selection completes successfully, we no longer need all indices to stick around. The selected
    // plan should safely die on yield recovery if it is using the dropped index.
    //
    // Dismiss the requirement that no indices can be dropped when this method returns.
    ON_BLOCK_EXIT([this] { releaseAllIndicesRequirement(); });

    // If we work this many times during the trial period, then we will replan the
    // query from scratch.
    size_t maxWorksBeforeReplan =
        static_cast<size_t>(internalQueryCacheEvictionRatio * _decisionWorks);

    // The trial period ends without replanning if the cached plan produces this many results.
    size_t numResults = MultiPlanStage::getTrialPeriodNumToReturn(*_canonicalQuery);

    for (size_t i = 0; i < maxWorksBeforeReplan; ++i) {
        // Might need to yield between calls to work due to the timer elapsing.
        Status yieldStatus = tryYield(yieldPolicy);
        if (!yieldStatus.isOK()) {
            return yieldStatus;
        }

        WorkingSetID id = WorkingSet::INVALID_ID;
        PlanStage::StageState state = child()->work(&id);

        if (PlanStage::ADVANCED == state) {
            // Save result for later.
            WorkingSetMember* member = _ws->get(id);
            // Ensure that the BSONObj underlying the WorkingSetMember is owned in case we yield.
            member->makeObjOwnedIfNeeded();
            _results.push(id);

            if (_results.size() >= numResults) {
                // Once a plan returns enough results, stop working. Update cache with stats
                // from this run and return.
                updatePlanCache();
                return Status::OK();
            }
        } else if (PlanStage::IS_EOF == state) {
            // Cached plan hit EOF quickly enough. No need to replan. Update cache with stats
            // from this run and return.
            updatePlanCache();
            return Status::OK();
        } else if (PlanStage::NEED_YIELD == state) {
            invariant(id == WorkingSet::INVALID_ID);
            if (!yieldPolicy->canAutoYield()) {
                throw WriteConflictException();
            }

            if (yieldPolicy->canAutoYield()) {
                yieldPolicy->forceYield();
            }

            Status yieldStatus = tryYield(yieldPolicy);
            if (!yieldStatus.isOK()) {
                return yieldStatus;
            }
        } else if (PlanStage::FAILURE == state) {
            // On failure, fall back to replanning the whole query. We neither evict the
            // existing cache entry nor cache the result of replanning.
            BSONObj statusObj;
            WorkingSetCommon::getStatusMemberObject(*_ws, id, &statusObj);

            LOG(1) << "Execution of cached plan failed, falling back to replan."
                   << " query: " << redact(_canonicalQuery->toStringShort())
                   << " planSummary: " << Explain::getPlanSummary(child().get())
                   << " status: " << redact(statusObj);

            const bool shouldCache = false;
            return replan(yieldPolicy, shouldCache);
        } else {
            invariant(PlanStage::NEED_TIME == state);
        }
    }

    // If we're here, the trial period took more than 'maxWorksBeforeReplan' work cycles. This
    // plan is taking too long, so we replan from scratch.
    LOG(1) << "Execution of cached plan required " << maxWorksBeforeReplan
           << " works, but was originally cached with only " << _decisionWorks
           << " works. Evicting cache entry and replanning query: "
           << redact(_canonicalQuery->toStringShort())
           << " plan summary before replan: " << Explain::getPlanSummary(child().get());

    const bool shouldCache = true;
    return replan(yieldPolicy, shouldCache);
}

Status CachedPlanStage::tryYield(PlanYieldPolicy* yieldPolicy) {
    // These are the conditions which can cause us to yield:
    //   1) The yield policy's timer elapsed, or
    //   2) some stage requested a yield, or
    //   3) we need to yield and retry due to a WriteConflictException.
    // In all cases, the actual yielding happens here.
    if (yieldPolicy->shouldYieldOrInterrupt()) {
        // Here's where we yield.
        return yieldPolicy->yieldOrInterrupt();
    }

    return Status::OK();
}

Status CachedPlanStage::replan(PlanYieldPolicy* yieldPolicy, bool shouldCache) {
    // We're going to start over with a new plan. Clear out info from our old plan.
    {
        std::queue<WorkingSetID> emptyQueue;
        _results.swap(emptyQueue);
    }
    _ws->clear();
    _children.clear();

    _specificStats.replanned = true;

    if (shouldCache) {
        // Deactivate the current cache entry.
        PlanCache* cache = collection()->infoCache()->getPlanCache();
        cache->deactivate(*_canonicalQuery);
    }

    // Use the query planning module to plan the whole query.
    auto statusWithSolutions = QueryPlanner::plan(*_canonicalQuery, _plannerParams);
    if (!statusWithSolutions.isOK()) {
        return statusWithSolutions.getStatus().withContext(
            str::stream() << "error processing query: " << _canonicalQuery->toString()
                          << " planner returned error");
    }

    auto solutions = std::move(statusWithSolutions.getValue());

    // We cannot figure out how to answer the query.  Perhaps it requires an index
    // we do not have?
    if (0 == solutions.size()) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << "error processing query: " << _canonicalQuery->toString()
                                    << " No query solutions");
    }

    if (1 == solutions.size()) {
        PlanStage* newRoot;
        // Only one possible plan. Build the stages from the solution.
        verify(StageBuilder::build(
            getOpCtx(), collection(), *_canonicalQuery, *solutions[0], _ws, &newRoot));
        _children.emplace_back(newRoot);
        _replannedQs = std::move(solutions.back());
        solutions.pop_back();

        LOG(1)
            << "Replanning of query resulted in single query solution, which will not be cached. "
            << redact(_canonicalQuery->toStringShort())
            << " plan summary after replan: " << Explain::getPlanSummary(child().get())
            << " previous cache entry evicted: " << (shouldCache ? "yes" : "no");
        return Status::OK();
    }

    // Many solutions. Create a MultiPlanStage to pick the best, update the cache,
    // and so on. The working set will be shared by all candidate plans.
    auto cachingMode = shouldCache ? MultiPlanStage::CachingMode::AlwaysCache
                                   : MultiPlanStage::CachingMode::NeverCache;
    _children.emplace_back(
        new MultiPlanStage(getOpCtx(), collection(), _canonicalQuery, cachingMode));
    MultiPlanStage* multiPlanStage = static_cast<MultiPlanStage*>(child().get());

    for (size_t ix = 0; ix < solutions.size(); ++ix) {
        if (solutions[ix]->cacheData.get()) {
            solutions[ix]->cacheData->indexFilterApplied = _plannerParams.indexFiltersApplied;
        }

        PlanStage* nextPlanRoot;
        verify(StageBuilder::build(
            getOpCtx(), collection(), *_canonicalQuery, *solutions[ix], _ws, &nextPlanRoot));

        // Takes ownership of 'nextPlanRoot'.
        multiPlanStage->addPlan(std::move(solutions[ix]), nextPlanRoot, _ws);
    }

    // Delegate to the MultiPlanStage's plan selection facility.
    Status pickBestPlanStatus = multiPlanStage->pickBestPlan(yieldPolicy);
    if (!pickBestPlanStatus.isOK()) {
        return pickBestPlanStatus;
    }

    LOG(1) << "Replanning " << redact(_canonicalQuery->toStringShort())
           << " resulted in plan with summary: " << Explain::getPlanSummary(child().get())
           << ", which " << (shouldCache ? "has" : "has not") << " been written to the cache";
    return Status::OK();
}

bool CachedPlanStage::isEOF() {
    return _results.empty() && child()->isEOF();
}

PlanStage::StageState CachedPlanStage::doWork(WorkingSetID* out) {
    if (isEOF()) {
        return PlanStage::IS_EOF;
    }

    // First exhaust any results buffered during the trial period.
    if (!_results.empty()) {
        *out = _results.front();
        _results.pop();
        return PlanStage::ADVANCED;
    }

    // Nothing left in trial period buffer.
    return child()->work(out);
}

std::unique_ptr<PlanStageStats> CachedPlanStage::getStats() {
    _commonStats.isEOF = isEOF();

    std::unique_ptr<PlanStageStats> ret =
        std::make_unique<PlanStageStats>(_commonStats, STAGE_CACHED_PLAN);
    ret->specific = std::make_unique<CachedPlanStats>(_specificStats);
    ret->children.emplace_back(child()->getStats());

    return ret;
}

const SpecificStats* CachedPlanStage::getSpecificStats() const {
    return &_specificStats;
}

void CachedPlanStage::updatePlanCache() {
    const double score = PlanRanker::scoreTree(getStats()->children[0].get());

    PlanCache* cache = collection()->infoCache()->getPlanCache();
    Status fbs = cache->feedback(*_canonicalQuery, score);
    if (!fbs.isOK()) {
        LOG(5) << _canonicalQuery->ns() << ": Failed to update cache with feedback: " << redact(fbs)
               << " - "
               << "(query: " << redact(_canonicalQuery->getQueryObj())
               << "; sort: " << _canonicalQuery->getQueryRequest().getSort()
               << "; projection: " << _canonicalQuery->getQueryRequest().getProj()
               << ") is no longer in plan cache.";
    }
}

}  // namespace monger
