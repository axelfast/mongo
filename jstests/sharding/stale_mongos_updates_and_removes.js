/**
 * Tests correctness of single and multi updates and removes sent by a *stale* mongers in the
 * absence of concurrent writes or migrations.
 *
 * Single updates and removes are always targeted and versioned, because they can be retried
 * without causing the operation to be repeated on another shard (only one shard can be originally
 * targeted for a single update or remove).
 *
 * Multi updates and removes containing an equality match on the shard key are also targeted and
 * versioned, because only one shard can be originally targeted for a point query on the shard key.
 *
 * All other multi updates and removes are sent to all shards and unversioned.
 *
 * This test is labeled resource intensive because its total io_write is 31MB compared to a median
 * of 5MB across all sharding tests in wiredTiger.
 *
 * @tags: [resource_intensive]
 */

(function() {
    'use strict';

    // Create a new sharded collection with numDocs documents, with two docs sharing each shard key
    // (used for testing *multi* removes to a *specific* shard key).
    function resetCollection() {
        assert(staleMongers.getCollection(collNS).drop());
        assert.commandWorked(staleMongers.adminCommand({shardCollection: collNS, key: {x: 1}}));

        for (let i = 0; i < numShardKeys; i++) {
            assert.writeOK(staleMongers.getCollection(collNS).insert({x: i, fieldToUpdate: 0}));
            assert.writeOK(staleMongers.getCollection(collNS).insert({x: i, fieldToUpdate: 0}));
        }

        // Make sure data has replicated to all config servers so freshMongers finds a sharded
        // collection: freshMongers has an older optime and won't wait to see what staleMongers did
        // (shardCollection).
        st.configRS.awaitLastOpCommitted();
    }

    // Create a new sharded collection, then split it into two chunks on different shards using the
    // stale mongers. Then use the fresh mongers to consolidate the chunks onto one of the shards.
    // staleMongers will see:
    //  shard0: (-inf, splitPoint]
    //  shard1: (splitPoint, inf]
    // freshMongers will see:
    //  shard0: (-inf, splitPoint], (splitPoint, inf]
    //  shard1:
    function makeStaleMongersTargetMultipleShardsWhenAllChunksAreOnOneShard() {
        resetCollection();

        // Make sure staleMongers sees all data on first shard.
        const chunk = staleMongers.getCollection("config.chunks")
                          .findOne({min: {x: MinKey}, max: {x: MaxKey}});
        assert(chunk.shard === st.shard0.shardName);

        // Make sure staleMongers sees two chunks on two different shards.
        assert.commandWorked(staleMongers.adminCommand({split: collNS, middle: {x: splitPoint}}));
        assert.commandWorked(staleMongers.adminCommand(
            {moveChunk: collNS, find: {x: 0}, to: st.shard1.shardName, _waitForDelete: true}));

        st.configRS.awaitLastOpCommitted();

        // Use freshMongers to consolidate the chunks on one shard.
        assert.commandWorked(freshMongers.adminCommand(
            {moveChunk: collNS, find: {x: 0}, to: st.shard0.shardName, _waitForDelete: true}));
    }

    // Create a new sharded collection with a single chunk, then move that chunk from the primary
    // shard to another shard using the fresh mongers.
    // staleMongers will see:
    //  shard0: (-inf, inf]
    //  shard1:
    // freshMongers will see:
    //  shard0:
    //  shard1: (-inf, inf]
    function makeStaleMongersTargetOneShardWhenAllChunksAreOnAnotherShard() {
        resetCollection();

        // Make sure staleMongers sees all data on first shard.
        const chunk = staleMongers.getCollection("config.chunks")
                          .findOne({min: {x: MinKey}, max: {x: MaxKey}});
        assert(chunk.shard === st.shard0.shardName);

        // Use freshMongers to move chunk to another shard.
        assert.commandWorked(freshMongers.adminCommand(
            {moveChunk: collNS, find: {x: 0}, to: st.shard1.shardName, _waitForDelete: true}));
    }

    // Create a new sharded collection, then split it into two chunks on different shards using the
    // fresh mongers.
    // staleMongers will see:
    //  shard0: (-inf, inf]
    //  shard1:
    // freshMongers will see:
    //  shard0: (-inf, splitPoint]
    //  shard1: (splitPoint, inf]
    function makeStaleMongersTargetOneShardWhenChunksAreOnMultipleShards() {
        resetCollection();

        // Make sure staleMongers sees all data on first shard.
        const chunk = staleMongers.getCollection("config.chunks")
                          .findOne({min: {x: MinKey}, max: {x: MaxKey}});
        assert(chunk.shard === st.shard0.shardName);

        // Use freshMongers to split and move chunks to both shards.
        assert.commandWorked(freshMongers.adminCommand({split: collNS, middle: {x: splitPoint}}));
        assert.commandWorked(freshMongers.adminCommand(
            {moveChunk: collNS, find: {x: 0}, to: st.shard1.shardName, _waitForDelete: true}));

        st.configRS.awaitLastOpCommitted();
    }

    function checkAllRemoveQueries(makeMongersStaleFunc) {
        const multi = {justOne: false};
        const single = {justOne: true};

        function doRemove(query, multiOption, makeMongersStaleFunc) {
            makeMongersStaleFunc();
            assert.writeOK(staleMongers.getCollection(collNS).remove(query, multiOption));
            if (multiOption.justOne) {
                // A total of one document should have been removed from the collection.
                assert.eq(numDocs - 1, staleMongers.getCollection(collNS).find().itcount());
            } else {
                // All documents matching the query should have been removed.
                assert.eq(0, staleMongers.getCollection(collNS).find(query).itcount());
            }
        }

        function checkRemoveIsInvalid(query, multiOption, makeMongersStaleFunc) {
            makeMongersStaleFunc();
            const res = staleMongers.getCollection(collNS).remove(query, multiOption);
            assert.writeError(res);
        }

        // Not possible because single remove requires equality match on shard key.
        checkRemoveIsInvalid(emptyQuery, single, makeMongersStaleFunc);
        doRemove(emptyQuery, multi, makeMongersStaleFunc);

        doRemove(pointQuery, single, makeMongersStaleFunc);
        doRemove(pointQuery, multi, makeMongersStaleFunc);

        // Not possible because can't do range query on a single remove.
        checkRemoveIsInvalid(rangeQuery, single, makeMongersStaleFunc);
        doRemove(rangeQuery, multi, makeMongersStaleFunc);

        // Not possible because single remove must contain _id or shard key at top level
        // (not within $or).
        checkRemoveIsInvalid(multiPointQuery, single, makeMongersStaleFunc);
        doRemove(multiPointQuery, multi, makeMongersStaleFunc);
    }

    function checkAllUpdateQueries(makeMongersStaleFunc) {
        const oUpdate = {$inc: {fieldToUpdate: 1}};  // op-style update (non-idempotent)
        const rUpdate = {x: 0, fieldToUpdate: 1};    // replacement-style update (idempotent)
        const queryAfterUpdate = {fieldToUpdate: 1};

        const multi = {multi: true};
        const single = {multi: false};

        function doUpdate(query, update, multiOption, makeMongersStaleFunc) {
            makeMongersStaleFunc();
            assert.writeOK(staleMongers.getCollection(collNS).update(query, update, multiOption));
            if (multiOption.multi) {
                // All documents matching the query should have been updated.
                assert.eq(staleMongers.getCollection(collNS).find(query).itcount(),
                          staleMongers.getCollection(collNS).find(queryAfterUpdate).itcount());
            } else {
                // A total of one document should have been updated.
                assert.eq(1, staleMongers.getCollection(collNS).find(queryAfterUpdate).itcount());
            }
        }

        function assertUpdateIsInvalid(query, update, multiOption, makeMongersStaleFunc) {
            makeMongersStaleFunc();
            const res = staleMongers.getCollection(collNS).update(query, update, multiOption);
            assert.writeError(res);
        }

        function assertUpdateIsValidIfAllChunksOnSingleShard(
            query, update, multiOption, makeMongersStaleFunc) {
            if (makeMongersStaleFunc == makeStaleMongersTargetOneShardWhenChunksAreOnMultipleShards) {
                assertUpdateIsInvalid(query, update, multiOption, makeMongersStaleFunc);
            } else {
                doUpdate(query, update, multiOption, makeMongersStaleFunc);
            }
        }

        // Note on the tests below: single-doc updates are able to succeed even in cases where the
        // stale mongerS incorrectly believes that the update targets multiple shards, because the
        // mongerS write path swallows the first error encountered in each batch, then internally
        // refreshes its routing table and tries the write again. Because all chunks are actually
        // on a single shard in two of the three test cases, this second update attempt succeeds.

        // This update has inconsistent behavior as explained in SERVER-22895.
        // doUpdate(emptyQuery, rUpdate, single, makeMongersStaleFunc);

        // Not possible because replacement-style requires equality match on shard key.
        assertUpdateIsInvalid(emptyQuery, rUpdate, multi, makeMongersStaleFunc);

        // Single op-style update succeeds if all chunks are on one shard, regardless of staleness.
        assertUpdateIsValidIfAllChunksOnSingleShard(
            emptyQuery, oUpdate, single, makeMongersStaleFunc);
        doUpdate(emptyQuery, oUpdate, multi, makeMongersStaleFunc);

        doUpdate(pointQuery, rUpdate, single, makeMongersStaleFunc);

        // Not possible because replacement-style requires multi=false.
        assertUpdateIsInvalid(pointQuery, rUpdate, multi, makeMongersStaleFunc);
        doUpdate(pointQuery, oUpdate, single, makeMongersStaleFunc);
        doUpdate(pointQuery, oUpdate, multi, makeMongersStaleFunc);

        doUpdate(rangeQuery, rUpdate, single, makeMongersStaleFunc);

        // Not possible because replacement-style requires multi=false.
        assertUpdateIsInvalid(rangeQuery, rUpdate, multi, makeMongersStaleFunc);

        // Range query for a single update succeeds because the range falls entirely on one shard.
        doUpdate(rangeQuery, oUpdate, single, makeMongersStaleFunc);
        doUpdate(rangeQuery, oUpdate, multi, makeMongersStaleFunc);

        doUpdate(multiPointQuery, rUpdate, single, makeMongersStaleFunc);

        // Not possible because replacement-style requires multi=false.
        assertUpdateIsInvalid(multiPointQuery, rUpdate, multi, makeMongersStaleFunc);

        // Multi-point single-doc update succeeds if all points are on a single shard.
        assertUpdateIsValidIfAllChunksOnSingleShard(
            multiPointQuery, oUpdate, single, makeMongersStaleFunc);
        doUpdate(multiPointQuery, oUpdate, multi, makeMongersStaleFunc);
    }

    // TODO: SERVER-33954 remove shardAsReplicaSet: false.
    const st = new ShardingTest({shards: 2, mongers: 2, other: {shardAsReplicaSet: false}});

    const dbName = 'test';
    const collNS = dbName + '.foo';
    const numShardKeys = 10;
    const numDocs = numShardKeys * 2;
    const splitPoint = numShardKeys / 2;

    assert.commandWorked(st.s.adminCommand({enableSharding: dbName}));
    assert.commandWorked(st.s.adminCommand({shardCollection: collNS, key: {x: 1}}));

    st.ensurePrimaryShard(dbName, st.shard0.shardName);

    const freshMongers = st.s0;
    const staleMongers = st.s1;

    const emptyQuery = {};
    const pointQuery = {x: 0};

    // Choose a range that would fall on only one shard.
    // Use (splitPoint - 1) because of SERVER-20768.
    const rangeQuery = {x: {$gte: 0, $lt: splitPoint - 1}};

    // Choose points that would fall on two different shards.
    const multiPointQuery = {$or: [{x: 0}, {x: numShardKeys}]};

    checkAllRemoveQueries(makeStaleMongersTargetOneShardWhenAllChunksAreOnAnotherShard);
    checkAllRemoveQueries(makeStaleMongersTargetMultipleShardsWhenAllChunksAreOnOneShard);

    checkAllUpdateQueries(makeStaleMongersTargetOneShardWhenAllChunksAreOnAnotherShard);
    checkAllUpdateQueries(makeStaleMongersTargetMultipleShardsWhenAllChunksAreOnOneShard);
    checkAllUpdateQueries(makeStaleMongersTargetOneShardWhenChunksAreOnMultipleShards);

    st.stop();
})();
