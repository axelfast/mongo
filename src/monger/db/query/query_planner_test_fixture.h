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
#include <ostream>
#include <string>
#include <vector>

#include "monger/base/owned_pointer_vector.h"
#include "monger/db/index/multikey_paths.h"
#include "monger/db/jsobj.h"
#include "monger/db/json.h"
#include "monger/db/query/collation/collator_interface.h"
#include "monger/db/query/query_solution.h"
#include "monger/db/query/query_test_service_context.h"
#include "monger/unittest/unittest.h"

namespace monger {

class QueryPlannerTest : public monger::unittest::Test {
protected:
    void setUp();

    /**
     * Clean up any previous state from a call to runQuery*()
     */
    void clearState();

    //
    // Build up test.
    //

    void addIndex(BSONObj keyPattern, bool multikey = false);

    void addIndex(BSONObj keyPattern, bool multikey, bool sparse);

    void addIndex(BSONObj keyPattern, bool multikey, bool sparse, bool unique);

    void addIndex(BSONObj keyPattern, BSONObj infoObj);

    void addIndex(BSONObj keyPattern, MatchExpression* filterExpr);

    void addIndex(BSONObj keyPattern, const MultikeyPaths& multikeyPaths);

    void addIndex(BSONObj keyPattern, const CollatorInterface* collator);

    void addIndex(BSONObj keyPattern,
                  MatchExpression* filterExpr,
                  const CollatorInterface* collator);

    void addIndex(BSONObj keyPattern, const CollatorInterface* collator, StringData indexName);

    void addIndex(const IndexEntry& ie);

    //
    // Execute planner.
    //

    void runQuery(BSONObj query);

    void runQuerySortProj(const BSONObj& query, const BSONObj& sort, const BSONObj& proj);

    void runQuerySkipNToReturn(const BSONObj& query, long long skip, long long ntoreturn);

    void runQueryHint(const BSONObj& query, const BSONObj& hint);

    void runQuerySortProjSkipNToReturn(const BSONObj& query,
                                       const BSONObj& sort,
                                       const BSONObj& proj,
                                       long long skip,
                                       long long ntoreturn);

    void runQuerySortHint(const BSONObj& query, const BSONObj& sort, const BSONObj& hint);

    void runQueryHintMinMax(const BSONObj& query,
                            const BSONObj& hint,
                            const BSONObj& minObj,
                            const BSONObj& maxObj);

    void runQuerySortProjSkipNToReturnHint(const BSONObj& query,
                                           const BSONObj& sort,
                                           const BSONObj& proj,
                                           long long skip,
                                           long long ntoreturn,
                                           const BSONObj& hint);

    void runQueryFull(const BSONObj& query,
                      const BSONObj& sort,
                      const BSONObj& proj,
                      long long skip,
                      long long ntoreturn,
                      const BSONObj& hint,
                      const BSONObj& minObj,
                      const BSONObj& maxObj);

    //
    // Same as runQuery* functions except we expect a failed status from the planning stage.
    //

    void runInvalidQuery(const BSONObj& query);

    void runInvalidQuerySortProj(const BSONObj& query, const BSONObj& sort, const BSONObj& proj);

    void runInvalidQuerySortProjSkipNToReturn(const BSONObj& query,
                                              const BSONObj& sort,
                                              const BSONObj& proj,
                                              long long skip,
                                              long long ntoreturn);

    void runInvalidQueryHint(const BSONObj& query, const BSONObj& hint);

    void runInvalidQueryHintMinMax(const BSONObj& query,
                                   const BSONObj& hint,
                                   const BSONObj& minObj,
                                   const BSONObj& maxObj);

    void runInvalidQuerySortProjSkipNToReturnHint(const BSONObj& query,
                                                  const BSONObj& sort,
                                                  const BSONObj& proj,
                                                  long long skip,
                                                  long long ntoreturn,
                                                  const BSONObj& hint);

    void runInvalidQueryFull(const BSONObj& query,
                             const BSONObj& sort,
                             const BSONObj& proj,
                             long long skip,
                             long long ntoreturn,
                             const BSONObj& hint,
                             const BSONObj& minObj,
                             const BSONObj& maxObj);

    /**
     * The other runQuery* methods run the query as through it is an OP_QUERY style find. This
     * version goes through find command parsing, and will be planned like a find command.
     */
    void runQueryAsCommand(const BSONObj& cmdObj);

    void runInvalidQueryAsCommand(const BSONObj& cmdObj);

    //
    // Introspect solutions.
    //

    size_t getNumSolutions() const;

    void dumpSolutions() const;

    void dumpSolutions(str::stream& ost) const;

    /**
     * Will use a relaxed bounds check for the remaining assert* calls. Subsequent calls to assert*
     * will check only that the bounds provided in the "expected" solution are a subset of those
     * generated by the planner (rather than checking for equality). Useful for testing queries
     * which use geo indexes and produce complex bounds.
     */
    void relaxBoundsCheckingToSubsetOnly() {
        invariant(!relaxBoundsCheck);
        relaxBoundsCheck = true;
    }

    /**
     * Checks number solutions. Generates assertion message
     * containing solution dump if applicable.
     */
    void assertNumSolutions(size_t expectSolutions) const;

    size_t numSolutionMatches(const std::string& solnJson) const;

    /**
     * Verifies that the solution tree represented in json by 'solnJson' is
     * one of the solutions generated by QueryPlanner.
     *
     * The number of expected matches, 'numMatches', could be greater than
     * 1 if solutions differ only by the pattern of index tags on a filter.
     */
    void assertSolutionExists(const std::string& solnJson, size_t numMatches = 1) const;

    /**
     * Given a vector of string-based solution tree representations 'solnStrs',
     * verifies that the query planner generated exactly one of these solutions.
     */
    void assertHasOneSolutionOf(const std::vector<std::string>& solnStrs) const;

    /**
     * Check that the only solution available is an ascending collection scan.
     */
    void assertHasOnlyCollscan() const;

    /**
     * Helper function to parse a MatchExpression.
     */
    static std::unique_ptr<MatchExpression> parseMatchExpression(
        const BSONObj& obj, const CollatorInterface* collator = nullptr);

    //
    // Data members.
    //

    static const NamespaceString nss;

    QueryTestServiceContext serviceContext;
    ServiceContext::UniqueOperationContext opCtx;
    BSONObj queryObj;
    std::unique_ptr<CanonicalQuery> cq;
    QueryPlannerParams params;
    std::vector<std::unique_ptr<QuerySolution>> solns;

    bool relaxBoundsCheck = false;
};

}  // namespace monger
