// Test clearing of the plan cache, either manually through the planCacheClear command,
// or due to system events such as an index build.
//
// @tags: [
//   # This test attempts to perform queries and introspect/manipulate the server's plan cache
//   # entries. The former operation may be routed to a secondary in the replica set, whereas the
//   # latter must be routed to the primary.
//   # If all chunks are moved off of a shard, it can cause the plan cache to miss commands.
//   assumes_read_preference_unchanged,
//   does_not_support_stepdowns,
//   assumes_balancer_off,
// ]

(function() {
    var t = db.jstests_plan_cache_clear;
    t.drop();

    // Utility function to list query shapes in cache.
    function getShapes(collection) {
        if (collection == undefined) {
            collection = t;
        }
        var res = collection.runCommand('planCacheListQueryShapes');
        print('planCacheListQueryShapes() = ' + tojson(res));
        assert.commandWorked(res, 'planCacheListQueryShapes failed');
        assert(res.hasOwnProperty('shapes'), 'shapes missing from planCacheListQueryShapes result');
        return res.shapes;
    }

    t.save({a: 1, b: 1});
    t.save({a: 1, b: 2});
    t.save({a: 1, b: 2});
    t.save({a: 2, b: 2});

    // We need two indices so that the MultiPlanRunner is executed.
    t.ensureIndex({a: 1});
    t.ensureIndex({a: 1, b: 1});

    // Run a query so that an entry is inserted into the cache.
    assert.eq(1, t.find({a: 1, b: 1}).itcount(), 'unexpected document count');

    // Invalid key should be a no-op.
    assert.commandWorked(t.runCommand('planCacheClear', {query: {unknownfield: 1}}));
    assert.eq(
        1, getShapes().length, 'removing unknown query should not affecting exisiting entries');

    // Run a new query shape and drop it from the cache
    assert.eq(1, getShapes().length, 'unexpected cache size after running 2nd query');
    assert.commandWorked(t.runCommand('planCacheClear', {query: {a: 1, b: 1}}));
    assert.eq(0, getShapes().length, 'unexpected cache size after dropping 2nd query from cache');

    // planCacheClear can clear $expr queries.
    assert.eq(
        1, t.find({a: 1, b: 1, $expr: {$eq: ['$a', 1]}}).itcount(), 'unexpected document count');
    assert.eq(1, getShapes().length, 'unexpected cache size after running 2nd query');
    assert.commandWorked(
        t.runCommand('planCacheClear', {query: {a: 1, b: 1, $expr: {$eq: ['$a', 1]}}}));
    assert.eq(0, getShapes().length, 'unexpected cache size after dropping 2nd query from cache');

    // planCacheClear fails with an $expr query with an unbound variable.
    assert.commandFailed(
        t.runCommand('planCacheClear', {query: {a: 1, b: 1, $expr: {$eq: ['$a', '$$unbound']}}}));

    // Insert two more shapes into the cache.
    assert.eq(1, t.find({a: 1, b: 1}).itcount(), 'unexpected document count');
    assert.eq(1, t.find({a: 1, b: 1}, {_id: 0, a: 1}).itcount(), 'unexpected document count');

    // Drop query cache. This clears all cached queries in the collection.
    res = t.runCommand('planCacheClear');
    print('planCacheClear() = ' + tojson(res));
    assert.commandWorked(res, 'planCacheClear failed');
    assert.eq(
        0, getShapes().length, 'plan cache should be empty after successful planCacheClear()');

    //
    // Query Plan Revision
    // http://docs.mongerdb.org/manual/core/query-plans/#query-plan-revision
    // As collections change over time, the query optimizer deletes the query plan and re-evaluates
    // after any of the following events:
    // - The reIndex rebuilds the index.
    // - You add or drop an index.
    // - The mongerd process restarts.
    //

    // Case 1: The reIndex rebuilds the index.
    // Steps:
    //     Populate the cache with 1 entry.
    //     Run reIndex on the collection.
    //     Confirm that cache is empty.
    const isMongers = db.adminCommand({isdbgrid: 1}).isdbgrid;
    if (!isMongers) {
        assert.eq(1, t.find({a: 1, b: 1}).itcount(), 'unexpected document count');
        assert.eq(1, getShapes().length, 'plan cache should not be empty after query');
        res = t.reIndex();
        print('reIndex result = ' + tojson(res));
        assert.eq(0, getShapes().length, 'plan cache should be empty after reIndex operation');
    }

    // Case 2: You add or drop an index.
    // Steps:
    //     Populate the cache with 1 entry.
    //     Add an index.
    //     Confirm that cache is empty.
    assert.eq(1, t.find({a: 1, b: 1}).itcount(), 'unexpected document count');
    assert.eq(1, getShapes().length, 'plan cache should not be empty after query');
    t.ensureIndex({b: 1});
    assert.eq(0, getShapes().length, 'plan cache should be empty after adding index');

    // Case 3: The mongerd process restarts
    // Not applicable.
})();
