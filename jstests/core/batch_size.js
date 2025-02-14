// @tags: [requires_getmore, assumes_balancer_off]

// Test subtleties of batchSize and limit.

(function() {
    "use strict";
    load("jstests/libs/fixture_helpers.js");  // For FixtureHelpers.
    var t = db.jstests_batch_size;
    t.drop();

    for (var i = 0; i < 4; i++) {
        t.save({_id: i, a: i});
    }

    function runIndexedTests() {
        // With limit, indexed.
        assert.eq(2, t.find().limit(2).itcount());
        assert.eq(2, t.find().sort({a: 1}).limit(2).itcount());

        // With batchSize, indexed.
        // SERVER-12438: If there is an index that provides the sort, then a plan with an unindexed
        // sort should never be used.  Consequently, batchSize will NOT be a hard limit in this
        // case.  WARNING: the behavior described above may change in the future.
        assert.eq(4, t.find().batchSize(2).itcount());
        assert.eq(4, t.find().sort({a: 1}).batchSize(2).itcount());
    }

    // Without batch size or limit, unindexed.
    assert.eq(4, t.find().itcount());
    assert.eq(4, t.find().sort({a: 1}).itcount());

    // With limit, unindexed.
    assert.eq(2, t.find().limit(2).itcount());
    assert.eq(2, t.find().sort({a: 1}).limit(2).itcount());

    assert.eq(4, t.find().batchSize(2).itcount());
    assert.eq(4, t.find().sort({a: 1}).batchSize(2).itcount());

    // With negative batchSize. A negative batchSize value instructs the server
    // to return just a single batch of results.
    assert.eq(1, t.find().batchSize(-1).itcount());
    assert.eq(2, t.find().batchSize(-2).itcount());

    // Run the tests with the index twice in order to double check plan caching.
    t.ensureIndex({a: 1});
    for (var i = 0; i < 2; i++) {
        runIndexedTests();
    }

    // The next tests make sure that we obey limit and batchSize properly when the sort could be
    // either indexed or unindexed.
    t.drop();
    t.ensureIndex({a: 1});
    t.ensureIndex({b: 1});

    for (var i = 0; i < 100; i++) {
        t.save({_id: i, a: i, b: 1});
    }

    // Without a hint. Do it twice to make sure caching is ok.
    for (var i = 0; i < 2; i++) {
        assert.eq(15, t.find({a: {$gte: 85}}).sort({b: 1}).batchSize(2).itcount());
        assert.eq(6, t.find({a: {$gte: 85}}).sort({b: 1}).limit(6).itcount());
    }

    // Hinting 'a'.
    assert.eq(15, t.find({a: {$gte: 85}}).sort({b: 1}).hint({a: 1}).batchSize(2).itcount());
    assert.eq(6, t.find({a: {$gte: 85}}).sort({b: 1}).hint({a: 1}).limit(6).itcount());

    // Hinting 'b'.
    assert.eq(15, t.find({a: {$gte: 85}}).sort({b: 1}).hint({b: 1}).batchSize(2).itcount());
    assert.eq(6, t.find({a: {$gte: 85}}).sort({b: 1}).hint({b: 1}).limit(6).itcount());

    // With explain.
    var explain = t.find({a: {$gte: 85}}).sort({b: 1}).batchSize(2).explain("executionStats");
    assert.eq(15, explain.executionStats.nReturned);
    explain = t.find({a: {$gte: 85}}).sort({b: 1}).limit(6).explain("executionStats");
    if (FixtureHelpers.isMongers(db)) {
        // If we're talking to a mongers, we expect at most one batch from each shard.
        assert.eq(FixtureHelpers.numberOfShardsForCollection(t) * 6,
                  explain.executionStats.nReturned);
    } else {
        assert.eq(6, explain.executionStats.nReturned);
    }

    // Double check that we're not scanning more stuff than we have to.  In order to get the sort
    // using index 'a', we should need to scan about 50 keys and 50 documents.
    var explain =
        t.find({a: {$gte: 50}}).sort({b: 1}).hint({a: 1}).limit(6).explain("executionStats");
    assert.lte(explain.executionStats.totalKeysExamined, 60);
    assert.lte(explain.executionStats.totalDocsExamined, 60);
    if (FixtureHelpers.isMongers(db)) {
        // If we're talking to a mongers, we expect at most one batch from each shard.
        assert.eq(FixtureHelpers.numberOfShardsForCollection(t) * 6,
                  explain.executionStats.nReturned);
    } else {
        assert.eq(6, explain.executionStats.nReturned);
    }

    // -------

    // During plan ranking, we treat ntoreturn as a limit. This prevents us from buffering too much
    // data in a blocking sort stage during plan ranking.
    t.drop();

    // Generate big string to use in the object - 1MB+ String
    var bigStr = "ABCDEFGHIJKLMNBOPQRSTUVWXYZ012345687890";
    while (bigStr.length < 1000000) {
        bigStr = bigStr + "::" + bigStr;
    }

    // Insert enough documents to exceed the 32 MB in-memory sort limit.
    const nDocs = 40 * FixtureHelpers.numberOfShardsForCollection(t);
    for (var i = 0; i < nDocs; i++) {
        var doc = {x: 1, y: 1, z: i, big: bigStr};
        t.insert(doc);
    }

    // Two indices needed in order to trigger plan ranking. Neither index provides the sort order.
    t.ensureIndex({x: 1});
    t.ensureIndex({y: 1});

    // We should only buffer 3 docs in memory.
    var cursor = t.find({x: 1, y: 1}).sort({z: -1}).limit(3);
    assert.eq(nDocs - 1, cursor.next().z);
    assert.eq(nDocs - 2, cursor.next().z);
    assert.eq(nDocs - 3, cursor.next().z);
    assert(!cursor.hasNext());
}());
