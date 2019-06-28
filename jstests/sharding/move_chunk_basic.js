//
// Basic tests for moveChunk.
//

(function() {
    'use strict';

    var st = new ShardingTest({mongers: 1, shards: 2});
    var kDbName = 'db';

    var mongers = st.s0;
    var shard0 = st.shard0.shardName;
    var shard1 = st.shard1.shardName;

    assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));
    st.ensurePrimaryShard(kDbName, shard0);

    // Fail if invalid namespace.
    assert.commandFailed(mongers.adminCommand({moveChunk: '', find: {_id: 1}, to: shard1}));

    // Fail if database does not exist.
    assert.commandFailed(mongers.adminCommand({moveChunk: 'a.b', find: {_id: 1}, to: shard1}));

    // Fail if collection is unsharded.
    assert.commandFailed(
        mongers.adminCommand({moveChunk: kDbName + '.xxx', find: {_id: 1}, to: shard1}));

    function testHashed() {
        var ns = kDbName + '.fooHashed';
        assert.commandWorked(mongers.adminCommand({shardCollection: ns, key: {_id: 'hashed'}}));

        var aChunk = mongers.getDB('config').chunks.findOne({_id: RegExp(ns), shard: shard0});
        assert(aChunk);

        // Error if either of the bounds is not a valid shard key (BSON object - 1 yields a NaN)
        assert.commandFailed(
            mongers.adminCommand({moveChunk: ns, bounds: [aChunk.min - 1, aChunk.max], to: shard1}));
        assert.commandFailed(
            mongers.adminCommand({moveChunk: ns, bounds: [aChunk.min, aChunk.max - 1], to: shard1}));

        // Fail if find and bounds are both set.
        assert.commandFailed(mongers.adminCommand(
            {moveChunk: ns, find: {_id: 1}, bounds: [aChunk.min, aChunk.max], to: shard1}));

        assert.commandWorked(
            mongers.adminCommand({moveChunk: ns, bounds: [aChunk.min, aChunk.max], to: shard1}));

        assert.eq(0, mongers.getDB('config').chunks.count({_id: aChunk._id, shard: shard0}));
        assert.eq(1, mongers.getDB('config').chunks.count({_id: aChunk._id, shard: shard1}));

        mongers.getDB(kDbName).fooHashed.drop();
    }

    function testNotHashed(keyDoc) {
        var ns = kDbName + '.foo';

        // Fail if find is not a valid shard key.
        assert.commandWorked(mongers.adminCommand({shardCollection: ns, key: keyDoc}));

        var chunkId = mongers.getDB('config').chunks.findOne({_id: RegExp(ns), shard: shard0})._id;

        assert.commandFailed(mongers.adminCommand({moveChunk: ns, find: {xxx: 1}, to: shard1}));
        assert.eq(shard0, mongers.getDB('config').chunks.findOne({_id: chunkId}).shard);

        assert.commandWorked(mongers.adminCommand({moveChunk: ns, find: keyDoc, to: shard1}));
        assert.eq(shard1, mongers.getDB('config').chunks.findOne({_id: chunkId}).shard);

        // Fail if to shard does not exists
        assert.commandFailed(mongers.adminCommand({moveChunk: ns, find: keyDoc, to: 'WrongShard'}));

        // Fail if chunk is already at shard
        assert.eq(shard1, mongers.getDB('config').chunks.findOne({_id: chunkId}).shard);

        mongers.getDB(kDbName).foo.drop();
    }

    testHashed();

    testNotHashed({a: 1});

    testNotHashed({a: 1, b: 1});

    st.stop();
})();
