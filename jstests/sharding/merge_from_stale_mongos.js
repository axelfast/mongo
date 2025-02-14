// Tests for $merge against a stale mongers with combinations of sharded/unsharded source and target
// collections.
(function() {
    "use strict";

    load("jstests/aggregation/extras/merge_helpers.js");  // For withEachMergeMode.
    load("jstests/aggregation/extras/utils.js");          // For assertErrorCode.

    const st = new ShardingTest({
        shards: 2,
        mongers: 4,
    });

    const freshMongers = st.s0.getDB(jsTestName());
    const staleMongersSource = st.s1.getDB(jsTestName());
    const staleMongersTarget = st.s2.getDB(jsTestName());
    const staleMongersBoth = st.s3.getDB(jsTestName());

    const sourceColl = freshMongers.getCollection("source");
    const targetColl = freshMongers.getCollection("target");

    // Enable sharding on the test DB and ensure its primary is shard 0.
    assert.commandWorked(
        staleMongersSource.adminCommand({enableSharding: staleMongersSource.getName()}));
    st.ensurePrimaryShard(staleMongersSource.getName(), st.rs0.getURL());

    // Shards the collection 'coll' through 'mongers'.
    function shardCollWithMongers(mongers, coll) {
        coll.drop();
        // Shard the given collection on _id, split the collection into 2 chunks: [MinKey, 0) and
        // [0, MaxKey), then move the [0, MaxKey) chunk to shard 1.
        assert.commandWorked(
            mongers.adminCommand({shardCollection: coll.getFullName(), key: {_id: 1}}));
        assert.commandWorked(mongers.adminCommand({split: coll.getFullName(), middle: {_id: 0}}));
        assert.commandWorked(mongers.adminCommand(
            {moveChunk: coll.getFullName(), find: {_id: 1}, to: st.rs1.getURL()}));
    }

    // Configures the two mongers, staleMongersSource and staleMongersTarget, to be stale on the source
    // and target collections, respectively. For instance, if 'shardedSource' is true then
    // staleMongersSource will believe that the source collection is unsharded.
    function setupStaleMongers({shardedSource, shardedTarget}) {
        // Initialize both mongers to believe the collections are unsharded.
        sourceColl.drop();
        targetColl.drop();
        assert.commandWorked(staleMongersSource[sourceColl.getName()].insert(
            {_id: "insert when unsharded (source)"}));
        assert.commandWorked(staleMongersSource[targetColl.getName()].insert(
            {_id: "insert when unsharded (source)"}));
        assert.commandWorked(staleMongersTarget[sourceColl.getName()].insert(
            {_id: "insert when unsharded (target)"}));
        assert.commandWorked(staleMongersTarget[targetColl.getName()].insert(
            {_id: "insert when unsharded (target)"}));

        if (shardedSource) {
            // Shard the source collection through the staleMongersTarget mongers, keeping the
            // staleMongersSource unaware.
            shardCollWithMongers(staleMongersTarget, sourceColl);
        } else {
            // Shard the collection through staleMongersSource.
            shardCollWithMongers(staleMongersSource, sourceColl);

            // Then drop the collection, but do not recreate it yet as that will happen on the next
            // insert later in the test.
            sourceColl.drop();
        }

        if (shardedTarget) {
            // Shard the target collection through the staleMongersSource mongers, keeping the
            // staleMongersTarget unaware.
            shardCollWithMongers(staleMongersSource, targetColl);
        } else {
            // Shard the collection through staleMongersTarget.
            shardCollWithMongers(staleMongersTarget, targetColl);

            // Then drop the collection, but do not recreate it yet as that will happen on the next
            // insert later in the test.
            targetColl.drop();
        }
    }

    // Runs a $merge with the given modes against each mongers in 'mongersList'. This method will wrap
    // 'mongersList' into a list if it is not an array.
    function runMergeTest(whenMatchedMode, whenNotMatchedMode, mongersList) {
        if (!(mongersList instanceof Array)) {
            mongersList = [mongersList];
        }

        mongersList.forEach(mongers => {
            targetColl.remove({});
            sourceColl.remove({});
            // Insert several documents into the source and target collection without any conflicts.
            // Note that the chunk split point is at {_id: 0}.
            assert.commandWorked(sourceColl.insert([{_id: -1}, {_id: 0}, {_id: 1}]));
            assert.commandWorked(targetColl.insert([{_id: -2}, {_id: 2}, {_id: 3}]));

            mongers[sourceColl.getName()].aggregate([{
                $merge: {
                    into: targetColl.getName(),
                    whenMatched: whenMatchedMode,
                    whenNotMatched: whenNotMatchedMode
                }
            }]);

            // If whenNotMatchedMode is "discard", then the documents in the source collection will
            // not get written to the target since none of them match.
            assert.eq(whenNotMatchedMode == "discard" ? 3 : 6, targetColl.find().itcount());
        });
    }

    withEachMergeMode(({whenMatchedMode, whenNotMatchedMode}) => {
        // Skip the combination of merge modes which will fail depending on the contents of the
        // source and target collection, as this will cause the assertion below to trip.
        if (whenNotMatchedMode == "fail")
            return;

        // For each mode, test the following scenarios:
        // * Both the source and target collections are sharded.
        // * Both the source and target collections are unsharded.
        // * Source collection is sharded and the target collection is unsharded.
        // * Source collection is unsharded and the target collection is sharded.
        setupStaleMongers({shardedSource: false, shardedTarget: false});
        runMergeTest(whenMatchedMode, whenNotMatchedMode, [staleMongersSource, staleMongersTarget]);

        setupStaleMongers({shardedSource: true, shardedTarget: true});
        runMergeTest(whenMatchedMode, whenNotMatchedMode, [staleMongersSource, staleMongersTarget]);

        setupStaleMongers({shardedSource: true, shardedTarget: false});
        runMergeTest(whenMatchedMode, whenNotMatchedMode, [staleMongersSource, staleMongersTarget]);

        setupStaleMongers({shardedSource: false, shardedTarget: true});
        runMergeTest(whenMatchedMode, whenNotMatchedMode, [staleMongersSource, staleMongersTarget]);

        //
        // The remaining tests run against a mongers which is stale with respect to BOTH the source
        // and target collections.
        //
        const sourceCollStale = staleMongersBoth.getCollection(sourceColl.getName());
        const targetCollStale = staleMongersBoth.getCollection(targetColl.getName());

        //
        // 1. Both source and target collections are sharded.
        //
        sourceCollStale.drop();
        targetCollStale.drop();

        // Insert into both collections through the stale mongers such that it believes the
        // collections exist and are unsharded.
        assert.commandWorked(sourceCollStale.insert({_id: 0}));
        assert.commandWorked(targetCollStale.insert({_id: 0}));

        shardCollWithMongers(freshMongers, sourceColl);
        shardCollWithMongers(freshMongers, targetColl);

        // Test against the stale mongers, which believes both collections are unsharded.
        runMergeTest(whenMatchedMode, whenNotMatchedMode, staleMongersBoth);

        //
        // 2. Both source and target collections are unsharded.
        //
        sourceColl.drop();
        targetColl.drop();

        // The collections were both dropped through a different mongers, so the stale mongers still
        // believes that they're sharded.
        runMergeTest(whenMatchedMode, whenNotMatchedMode, staleMongersBoth);

        //
        // 3. Source collection is sharded and target collection is unsharded.
        //
        sourceCollStale.drop();

        // Insert into the source collection through the stale mongers such that it believes the
        // collection exists and is unsharded.
        assert.commandWorked(sourceCollStale.insert({_id: 0}));

        // Shard the source collection through the fresh mongers.
        shardCollWithMongers(freshMongers, sourceColl);

        // Shard the target through the stale mongers, but then drop and recreate it as unsharded
        // through a different mongers.
        shardCollWithMongers(staleMongersBoth, targetColl);
        targetColl.drop();

        // At this point, the stale mongers believes the source collection is unsharded and the
        // target collection is sharded when in fact the reverse is true.
        runMergeTest(whenMatchedMode, whenNotMatchedMode, staleMongersBoth);

        //
        // 4. Source collection is unsharded and target collection is sharded.
        //
        sourceCollStale.drop();
        targetCollStale.drop();

        // Insert into the target collection through the stale mongers such that it believes the
        // collection exists and is unsharded.
        assert.commandWorked(targetCollStale.insert({_id: 0}));

        shardCollWithMongers(freshMongers, targetColl);

        // Shard the source through the stale mongers, but then drop and recreate it as unsharded
        // through a different mongers.
        shardCollWithMongers(staleMongersBoth, sourceColl);
        sourceColl.drop();

        // At this point, the stale mongers believes the source collection is sharded and the target
        // collection is unsharded when in fact the reverse is true.
        runMergeTest(whenMatchedMode, whenNotMatchedMode, staleMongersBoth);
    });

    // Runs a legacy $out against each mongers in 'mongersList'. This method will wrap 'mongersList'
    // into a list if it is not an array.
    function runOutTest(mongersList) {
        if (!(mongersList instanceof Array)) {
            mongersList = [mongersList];
        }

        mongersList.forEach(mongers => {
            targetColl.remove({});
            sourceColl.remove({});
            // Insert several documents into the source and target collection without any conflicts.
            // Note that the chunk split point is at {_id: 0}.
            assert.commandWorked(sourceColl.insert([{_id: -1}, {_id: 0}, {_id: 1}]));
            assert.commandWorked(targetColl.insert([{_id: -2}, {_id: 2}, {_id: 3}]));

            mongers[sourceColl.getName()].aggregate([{$out: targetColl.getName()}]);
            assert.eq(3, targetColl.find().itcount());
        });
    }

    // Legacy $out will fail if the target collection is sharded.
    setupStaleMongers({shardedSource: false, shardedTarget: false});
    runOutTest([staleMongersSource, staleMongersTarget]);

    setupStaleMongers({shardedSource: true, shardedTarget: true});
    assert.eq(assert.throws(() => runOutTest(staleMongersSource)).code, 28769);
    assert.eq(assert.throws(() => runOutTest(staleMongersTarget)).code, 17017);

    setupStaleMongers({shardedSource: true, shardedTarget: false});
    runOutTest([staleMongersSource, staleMongersTarget]);

    setupStaleMongers({shardedSource: false, shardedTarget: true});
    assert.eq(assert.throws(() => runOutTest(staleMongersSource)).code, 28769);
    assert.eq(assert.throws(() => runOutTest(staleMongersTarget)).code, 17017);

    st.stop();
}());
