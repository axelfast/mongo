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

#include <boost/optional/optional.hpp>
#include <set>

#include "monger/db/exec/plan_stats.h"
#include "monger/db/query/canonical_query.h"
#include "monger/db/query/index_tag.h"
#include "monger/db/query/lru_key_value.h"
#include "monger/db/query/plan_cache_indexability.h"
#include "monger/db/query/query_planner_params.h"
#include "monger/platform/atomic_word.h"
#include "monger/stdx/mutex.h"

namespace monger {

/**
 * Represents the "key" used in the PlanCache mapping from query shape -> query plan.
 */
class PlanCacheKey {
public:
    PlanCacheKey(CanonicalQuery::QueryShapeString shapeString, std::string indexabilityString) {
        _lengthOfStablePart = shapeString.size();
        _key = std::move(shapeString);
        _key += indexabilityString;
    }

    CanonicalQuery::QueryShapeString getStableKey() const {
        return std::string(_key, 0, _lengthOfStablePart);
    }

    StringData getStableKeyStringData() const {
        return StringData(_key.c_str(), _lengthOfStablePart);
    }

    /**
     * Return the "unstable" portion of the key, which may vary across catalog changes.
     */
    StringData getUnstablePart() const {
        return StringData(_key.c_str() + _lengthOfStablePart, _key.size() - _lengthOfStablePart);
    }

    StringData stringData() const {
        return _key;
    }

    const std::string& toString() const {
        return _key;
    }

    bool operator==(const PlanCacheKey& other) const {
        return other._key == _key && other._lengthOfStablePart == _lengthOfStablePart;
    }

    bool operator!=(const PlanCacheKey& other) const {
        return !(*this == other);
    }

private:
    // Key is broken into two parts:
    // <stable key> | <indexability discriminators>
    // Combined, the two parts make up the plan cache key. We store them in one std::string so that
    // we can easily/cheaply extract the stable key.
    std::string _key;

    // How long the "stable key" is.
    size_t _lengthOfStablePart;
};

std::ostream& operator<<(std::ostream& stream, const PlanCacheKey& key);
StringBuilder& operator<<(StringBuilder& builder, const PlanCacheKey& key);

class PlanCacheKeyHasher {
public:
    std::size_t operator()(const PlanCacheKey& k) const {
        return std::hash<std::string>{}(k.toString());
    }
};


struct PlanRankingDecision;
struct QuerySolution;
struct QuerySolutionNode;

/**
 * A PlanCacheIndexTree is the meaty component of the data
 * stored in SolutionCacheData. It is a tree structure with
 * index tags that indicates to the access planner which indices
 * it should try to use.
 *
 * How a PlanCacheIndexTree is created:
 *   The query planner tags a match expression with indices. It
 *   then uses the tagged tree to create a PlanCacheIndexTree,
 *   using QueryPlanner::cacheDataFromTaggedTree. The PlanCacheIndexTree
 *   is isomorphic to the tagged match expression, and has matching
 *   index tags.
 *
 * How a PlanCacheIndexTree is used:
 *   When the query planner is planning from the cache, it uses
 *   the PlanCacheIndexTree retrieved from the cache in order to
 *   recreate index assignments. Specifically, a raw MatchExpression
 *   is tagged according to the index tags in the PlanCacheIndexTree.
 *   This is done by QueryPlanner::tagAccordingToCache.
 */
struct PlanCacheIndexTree {

    /**
     * An OrPushdown is the cached version of an OrPushdownTag::Destination. It indicates that this
     * node is a predicate that can be used inside of a sibling indexed OR, to tighten index bounds
     * or satisfy the first field in the index.
     */
    struct OrPushdown {
        IndexEntry::Identifier indexEntryId;
        size_t position;
        bool canCombineBounds;
        std::deque<size_t> route;
    };

    PlanCacheIndexTree() : entry(nullptr), index_pos(0), canCombineBounds(true) {}

    ~PlanCacheIndexTree() {
        for (std::vector<PlanCacheIndexTree*>::const_iterator it = children.begin();
             it != children.end();
             ++it) {
            delete *it;
        }
    }

    /**
     * Clone 'ie' and set 'this->entry' to be the clone.
     */
    void setIndexEntry(const IndexEntry& ie);

    /**
     * Make a deep copy.
     */
    PlanCacheIndexTree* clone() const;

    /**
     * For debugging.
     */
    std::string toString(int indents = 0) const;

    // Children owned here.
    std::vector<PlanCacheIndexTree*> children;

    // Owned here.
    std::unique_ptr<IndexEntry> entry;

    size_t index_pos;

    // The value for this member is taken from the IndexTag of the corresponding match expression
    // and is used to ensure that bounds are correctly intersected and/or compounded when a query is
    // planned from the plan cache.
    bool canCombineBounds;

    std::vector<OrPushdown> orPushdowns;
};

/**
 * Data stored inside a QuerySolution which can subsequently be
 * used to create a cache entry. When this data is retrieved
 * from the cache, it is sufficient to reconstruct the original
 * QuerySolution.
 */
struct SolutionCacheData {
    SolutionCacheData()
        : tree(nullptr),
          solnType(USE_INDEX_TAGS_SOLN),
          wholeIXSolnDir(1),
          indexFilterApplied(false) {}

    // Make a deep copy.
    SolutionCacheData* clone() const;

    // For debugging.
    std::string toString() const;

    // Owned here. If 'wholeIXSoln' is false, then 'tree'
    // can be used to tag an isomorphic match expression. If 'wholeIXSoln'
    // is true, then 'tree' is used to store the relevant IndexEntry.
    // If 'collscanSoln' is true, then 'tree' should be NULL.
    std::unique_ptr<PlanCacheIndexTree> tree;

    enum SolutionType {
        // Indicates that the plan should use
        // the index as a proxy for a collection
        // scan (e.g. using index to provide sort).
        WHOLE_IXSCAN_SOLN,

        // The cached plan is a collection scan.
        COLLSCAN_SOLN,

        // Build the solution by using 'tree'
        // to tag the match expression.
        USE_INDEX_TAGS_SOLN
    } solnType;

    // The direction of the index scan used as
    // a proxy for a collection scan. Used only
    // for WHOLE_IXSCAN_SOLN.
    int wholeIXSolnDir;

    // True if index filter was applied.
    bool indexFilterApplied;
};

class PlanCacheEntry;

/**
 * Information returned from a get(...) query.
 */
class CachedSolution {
private:
    CachedSolution(const CachedSolution&) = delete;
    CachedSolution& operator=(const CachedSolution&) = delete;

public:
    CachedSolution(const PlanCacheKey& key, const PlanCacheEntry& entry);
    ~CachedSolution();

    // Owned here.
    std::vector<SolutionCacheData*> plannerData;

    // Key used to provide feedback on the entry.
    PlanCacheKey key;

    // For debugging.
    std::string toString() const;

    // We are extracting just enough information from the canonical
    // query. We could clone the canonical query but the following
    // items are all that is displayed to the user.
    BSONObj query;
    BSONObj sort;
    BSONObj projection;
    BSONObj collation;

    // The number of work cycles taken to decide on a winning plan when the plan was first
    // cached.
    size_t decisionWorks;
};

/**
 * Used by the cache to track entries and their performance over time.
 * Also used by the plan cache commands to display plan cache state.
 */
class PlanCacheEntry {
private:
    PlanCacheEntry(const PlanCacheEntry&) = delete;
    PlanCacheEntry& operator=(const PlanCacheEntry&) = delete;

public:
    /**
     * Create a new PlanCacheEntry.
     * Grabs any planner-specific data required from the solutions.
     * Takes ownership of the PlanRankingDecision that placed the plan in the cache.
     */
    PlanCacheEntry(const std::vector<QuerySolution*>& solutions,
                   PlanRankingDecision* why,
                   uint32_t queryHash,
                   uint32_t planCacheKey);

    ~PlanCacheEntry();

    /**
     * Make a deep copy.
     */
    PlanCacheEntry* clone() const;

    // For debugging.
    std::string toString() const;

    //
    // Planner data
    //

    // Data provided to the planner to allow it to recreate the solutions this entry
    // represents. Each SolutionCacheData is fully owned here, so in order to return
    // it from the cache a deep copy is made and returned inside CachedSolution.
    std::vector<SolutionCacheData*> plannerData;

    // TODO: Do we really want to just hold a copy of the CanonicalQuery?  For now we just
    // extract the data we need.
    //
    // Used by the plan cache commands to display an example query
    // of the appropriate shape.
    BSONObj query;
    BSONObj sort;
    BSONObj projection;
    BSONObj collation;
    Date_t timeOfCreation;

    // Hash of the PlanCacheKey. Intended as an identifier for the query shape in logs and other
    // diagnostic output.
    uint32_t queryHash;

    // Hash of the "stable" PlanCacheKey, which is the same regardless of what indexes are around.
    uint32_t planCacheKey;

    //
    // Performance stats
    //

    // Information that went into picking the winning plan and also why
    // the other plans lost.
    std::unique_ptr<PlanRankingDecision> decision;

    // Scores from uses of this cache entry.
    std::vector<double> feedback;

    // Whether or not the cache entry is active. Inactive cache entries should not be used for
    // planning.
    bool isActive = false;

    // The number of "works" required for a plan to run on this shape before it becomes
    // active. This value is also used to determine the number of works necessary in order to
    // trigger a replan. Running a query of the same shape while this cache entry is inactive may
    // cause this value to be increased.
    size_t works = 0;
};

/**
 * Caches the best solution to a query.  Aside from the (CanonicalQuery -> QuerySolution)
 * mapping, the cache contains information on why that mapping was made and statistics on the
 * cache entry's actual performance on subsequent runs.
 */
class PlanCache {
private:
    PlanCache(const PlanCache&) = delete;
    PlanCache& operator=(const PlanCache&) = delete;

public:
    // We have three states for a cache entry to be in. Rather than just 'present' or 'not
    // present', we use a notion of 'inactive entries' as a way of remembering how performant our
    // original solution to the query was. This information is useful to prevent much slower
    // queries from putting their plans in the cache immediately, which could cause faster queries
    // to run with a sub-optimal plan. Since cache entries must go through the "vetting" process of
    // being inactive, we protect ourselves from the possibility of simply adding a cache entry
    // with a very high works value which will never be evicted.
    enum CacheEntryState {
        // There is no cache entry for the given query shape.
        kNotPresent,

        // There is a cache entry for the given query shape, but it is inactive, meaning that it
        // should not be used when planning.
        kPresentInactive,

        // There is a cache entry for the given query shape, and it is active.
        kPresentActive,
    };

    /**
     * Encapsulates the value returned from a call to get().
     */
    struct GetResult {
        CacheEntryState state;
        std::unique_ptr<CachedSolution> cachedSolution;
    };

    /**
     * We don't want to cache every possible query. This function
     * encapsulates the criteria for what makes a canonical query
     * suitable for lookup/inclusion in the cache.
     */
    static bool shouldCacheQuery(const CanonicalQuery& query);

    /**
     * If omitted, namespace set to empty string.
     */
    PlanCache();

    PlanCache(size_t size);

    PlanCache(const std::string& ns);

    ~PlanCache();

    /**
     * Record solutions for query. Best plan is first element in list.
     * Each query in the cache will have more than 1 plan because we only
     * add queries which are considered by the multi plan runner (which happens
     * only when the query planner generates multiple candidate plans). Callers are responsible
     * for passing the current time so that the time the plan cache entry was created is stored
     * in the plan cache.
     *
     * 'worksGrowthCoefficient' specifies what multiplier to use when growing the 'works' value of
     * an inactive cache entry.  If boost::none is provided, the function will use
     * 'internalQueryCacheWorksGrowthCoefficient'.
     *
     * If the mapping was set successfully, returns Status::OK(), even if it evicted another entry.
     */
    Status set(const CanonicalQuery& query,
               const std::vector<QuerySolution*>& solns,
               std::unique_ptr<PlanRankingDecision> why,
               Date_t now,
               boost::optional<double> worksGrowthCoefficient = boost::none);

    /**
     * Set a cache entry back to the 'inactive' state. Rather than completely evicting an entry
     * when the associated plan starts to perform poorly, we deactivate it, so that plans which
     * perform even worse than the one already in the cache may not easily take its place.
     */
    void deactivate(const CanonicalQuery& query);

    /**
     * Look up the cached data access for the provided 'query'.  Used by the query planner
     * to shortcut planning.
     *
     * The return value will provide the "state" of the cache entry, as well as the CachedSolution
     * for the query (if there is one).
     */
    GetResult get(const CanonicalQuery& query) const;

    /**
     * Look up the cached data access for the provided PlanCacheKey. Circumvents the recalculation
     * of a plan cache key.
     *
     * The return value will provide the "state" of the cache entry, as well as the CachedSolution
     * for the query (if there is one).
     */
    GetResult get(const PlanCacheKey& key) const;

    /**
     * If the cache entry exists and is active, return a CachedSolution. If the cache entry is
     * inactive, log a message and return a nullptr. If no cache entry exists, return a nullptr.
     */
    std::unique_ptr<CachedSolution> getCacheEntryIfActive(const PlanCacheKey& key) const;


    /**
     * When the CachedPlanStage runs a plan out of the cache, we want to record data about the
     * plan's performance. The CachedPlanStage calls feedback(...) after executing the cached
     * plan for a trial period in order to do this. Currently, the only feedback metric recorded is
     * the score associated with the cached plan trial period.
     *
     * If the entry corresponding to 'cq' isn't in the cache anymore, the feedback is ignored
     * and an error Status is returned.
     *
     * If the entry corresponding to 'cq' still exists, 'feedback' is added to the run
     * statistics about the plan.  Status::OK() is returned.
     */
    Status feedback(const CanonicalQuery& cq, double score);

    /**
     * Remove the entry corresponding to 'ck' from the cache.  Returns Status::OK() if the plan
     * was present and removed and an error status otherwise.
     */
    Status remove(const CanonicalQuery& canonicalQuery);

    /**
     * Remove *all* cached plans.  Does not clear index information.
     */
    void clear();

    /**
     * Get the cache key corresponding to the given canonical query.  The query need not already
     * be cached.
     *
     * This is provided in the public API simply as a convenience for consumers who need some
     * description of query shape (e.g. index filters).
     *
     * Callers must hold the collection lock when calling this method.
     */
    PlanCacheKey computeKey(const CanonicalQuery&) const;

    /**
     * Returns a copy of a cache entry.
     * Used by planCacheListPlans to display plan details.
     *
     * If there is no entry in the cache for the 'query', returns an error Status.
     */
    StatusWith<std::unique_ptr<PlanCacheEntry>> getEntry(const CanonicalQuery& cq) const;

    /**
     * Returns a vector of all cache entries.
     * Used by planCacheListQueryShapes and index_filter_commands_test.cpp.
     */
    std::vector<std::unique_ptr<PlanCacheEntry>> getAllEntries() const;

    /**
     * Returns number of entries in cache. Includes inactive entries.
     * Used for testing.
     */
    size_t size() const;

    /**
     * Updates internal state kept about the collection's indexes.  Must be called when the set
     * of indexes on the associated collection have changed.
     *
     * Callers must hold the collection lock in exclusive mode when calling this method.
     */
    void notifyOfIndexUpdates(const std::vector<CoreIndexInfo>& indexCores);

    /**
     * Iterates over the plan cache. For each entry, serializes the PlanCacheEntry according to
     * 'serializationFunc'. Returns a vector of all serialized entries which match 'filterFunc'.
     */
    std::vector<BSONObj> getMatchingStats(
        const std::function<BSONObj(const PlanCacheEntry&)>& serializationFunc,
        const std::function<bool(const BSONObj&)>& filterFunc) const;

    void setNs(NamespaceString ns) {
        _ns = ns.toString();
    }

private:
    struct NewEntryState {
        bool shouldBeCreated = false;
        bool shouldBeActive = false;
    };

    NewEntryState getNewEntryState(const CanonicalQuery& query,
                                   uint32_t queryHash,
                                   uint32_t planCacheKey,
                                   PlanCacheEntry* oldEntry,
                                   size_t newWorks,
                                   double growthCoefficient);

    LRUKeyValue<PlanCacheKey, PlanCacheEntry, PlanCacheKeyHasher> _cache;

    // Protects _cache.
    mutable stdx::mutex _cacheMutex;

    // Full namespace of collection.
    std::string _ns;

    // Holds computed information about the collection's indexes.  Used for generating plan
    // cache keys.
    //
    // Concurrent access is synchronized by the collection lock.  Multiple concurrent readers
    // are allowed.
    PlanCacheIndexabilityState _indexabilityState;
};

}  // namespace monger
