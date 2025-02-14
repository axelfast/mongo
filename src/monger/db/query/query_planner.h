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

#include "monger/base/string_data.h"
#include "monger/db/query/canonical_query.h"
#include "monger/db/query/query_planner_params.h"
#include "monger/db/query/query_solution.h"

namespace monger {

class CachedSolution;
class Collection;

/**
 * QueryPlanner's job is to provide an entry point to the query planning and optimization
 * process.
 */
class QueryPlanner {
public:
    // Identifies the version of the query planner module. Reported in explain.
    static const int kPlannerVersion;

    /**
     * Returns the list of possible query solutions for the provided 'query'. Uses the indices and
     * other data in 'params' to determine the set of available plans.
     */
    static StatusWith<std::vector<std::unique_ptr<QuerySolution>>> plan(
        const CanonicalQuery& query, const QueryPlannerParams& params);

    /**
     * Generates and returns a query solution, given data retrieved from the plan cache.
     *
     * @param query -- query for which we are generating a plan
     * @param params -- planning parameters
     * @param cachedSoln -- the CachedSolution retrieved from the plan cache.
     */
    static StatusWith<std::unique_ptr<QuerySolution>> planFromCache(
        const CanonicalQuery& query,
        const QueryPlannerParams& params,
        const CachedSolution& cachedSoln);

    /**
     * Generates and returns the index tag tree that will be inserted into the plan cache. This data
     * gets stashed inside a QuerySolution until it can be inserted into the cache proper.
     *
     * @param taggedTree -- a MatchExpression with index tags that has been
     *   produced by the enumerator.
     * @param relevantIndices -- a list of the index entries used to tag
     *   the tree (i.e. index numbers in the tags refer to entries in this vector)
     */
    static StatusWith<std::unique_ptr<PlanCacheIndexTree>> cacheDataFromTaggedTree(
        const MatchExpression* const taggedTree, const std::vector<IndexEntry>& relevantIndices);

    /**
     * @param filter -- an untagged MatchExpression
     * @param indexTree -- a tree structure retrieved from the
     *   cache with index tags that indicates how 'filter' should
     *   be tagged.
     * @param indexMap -- needed in order to put the proper index
     *   numbers inside the index tags
     *
     * On success, 'filter' is mutated so that it has all the
     * index tags needed in order for the access planner to recreate
     * the cached plan.
     *
     * On failure, the tag state attached to the nodes of 'filter'
     * is invalid. Planning from the cache should be aborted.
     *
     * Does not take ownership of either filter or indexTree.
     */
    static Status tagAccordingToCache(MatchExpression* filter,
                                      const PlanCacheIndexTree* const indexTree,
                                      const std::map<IndexEntry::Identifier, size_t>& indexMap);
};

}  // namespace monger
