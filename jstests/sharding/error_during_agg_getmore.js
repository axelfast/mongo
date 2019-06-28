// This test was designed to reproduce SERVER-31475. It issues sharded aggregations with an error
// returned from one shard, and a delayed response from another shard.
(function() {
    "use strict";

    const st = new ShardingTest({shards: 2, useBridge: true});

    const mongersDB = st.s0.getDB(jsTestName());
    const mongersColl = mongersDB[jsTestName()];

    assert.commandWorked(mongersDB.dropDatabase());

    // Enable sharding on the test DB and ensure its primary is st.shard0.shardName.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.shard0.shardName);

    // Shard the test collection on _id.
    assert.commandWorked(
        mongersDB.adminCommand({shardCollection: mongersColl.getFullName(), key: {_id: 1}}));

    // Split the collection into 2 chunks: [MinKey, 0), [0, MaxKey].
    assert.commandWorked(
        mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {_id: 0}}));

    // Move the [0, MaxKey] chunk to st.shard1.shardName.
    assert.commandWorked(mongersDB.adminCommand(
        {moveChunk: mongersColl.getFullName(), find: {_id: 1}, to: st.shard1.shardName}));

    // Write a document to each chunk.
    assert.writeOK(mongersColl.insert({_id: -1}));
    assert.writeOK(mongersColl.insert({_id: 1}));

    // Delay messages between shard 1 and the mongers, long enough that shard 1's responses will
    // likely arrive after the response from shard 0, but not so long that the background cluster
    // client cleanup job will have been given a chance to run.
    const delayMillis = 100;
    st.rs1.getPrimary().delayMessagesFrom(st.s, delayMillis);

    const nTrials = 10;
    for (let i = 1; i < 10; ++i) {
        // This will trigger an error on shard 0, but not shard 1. We set up a delay from shard 1,
        // so the response should get back after the error has been returned to the client. We use a
        // batch size of 0 to ensure the error happens during a getMore.
        assert.throws(
            () => mongersColl
                      .aggregate([{$project: {_id: 0, x: {$divide: [2, {$add: ["$_id", 1]}]}}}],
                                 {cursor: {batchSize: 0}})
                      .itcount());
    }

    st.stop();
}());
