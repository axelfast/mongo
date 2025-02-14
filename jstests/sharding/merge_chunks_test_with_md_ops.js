// Tests that merging chunks does not prevent cluster from doing other metadata ops

(function() {
    'use strict';

    var st = new ShardingTest({shards: 2});

    var mongers = st.s0;
    var admin = mongers.getDB("admin");
    var coll = mongers.getCollection("foo.bar");

    assert.commandWorked(admin.runCommand({enableSharding: coll.getDB() + ""}));
    st.ensurePrimaryShard(coll.getDB() + "", st.shard0.shardName);
    assert.commandWorked(admin.runCommand({shardCollection: coll + "", key: {_id: 1}}));

    st.printShardingStatus();

    // Split and merge the first chunk repeatedly
    jsTest.log("Splitting and merging repeatedly...");

    for (var i = 0; i < 5; i++) {
        assert.commandWorked(admin.runCommand({split: coll + "", middle: {_id: i}}));
        assert.commandWorked(
            admin.runCommand({mergeChunks: coll + "", bounds: [{_id: MinKey}, {_id: MaxKey}]}));
        printjson(mongers.getDB("config").chunks.find().toArray());
    }

    // Move the first chunk to the other shard
    jsTest.log("Moving to another shard...");

    assert.commandWorked(
        admin.runCommand({moveChunk: coll + "", find: {_id: 0}, to: st.shard1.shardName}));

    // Split and merge the chunk repeatedly
    jsTest.log("Splitting and merging repeatedly (again)...");

    for (var i = 0; i < 5; i++) {
        assert.commandWorked(admin.runCommand({split: coll + "", middle: {_id: i}}));
        assert.commandWorked(
            admin.runCommand({mergeChunks: coll + "", bounds: [{_id: MinKey}, {_id: MaxKey}]}));
        printjson(mongers.getDB("config").chunks.find().toArray());
    }

    // Move the chunk back to the original shard
    jsTest.log("Moving to original shard...");

    assert.commandWorked(
        admin.runCommand({moveChunk: coll + "", find: {_id: 0}, to: st.shard0.shardName}));

    st.printShardingStatus();

    st.stop();

})();
