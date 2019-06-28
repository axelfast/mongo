/**
 * Tests whether new sharding is detected on insert by mongers
 */
(function() {
    'use strict';

    // TODO: SERVER-33830 remove shardAsReplicaSet: false
    var st = new ShardingTest({shards: 10, mongers: 3, other: {shardAsReplicaSet: false}});

    var mongersA = st.s0;
    var mongersB = st.s1;
    var mongersC = st.s2;

    var admin = mongersA.getDB("admin");
    var config = mongersA.getDB("config");

    var collA = mongersA.getCollection("foo.bar");
    var collB = mongersB.getCollection("" + collA);
    var collC = mongersB.getCollection("" + collA);

    var shards = [
        st.shard0,
        st.shard1,
        st.shard2,
        st.shard3,
        st.shard4,
        st.shard5,
        st.shard6,
        st.shard7,
        st.shard8,
        st.shard9
    ];

    assert.commandWorked(admin.runCommand({enableSharding: "" + collA.getDB()}));
    st.ensurePrimaryShard(collA.getDB().getName(), st.shard1.shardName);
    assert.commandWorked(admin.runCommand({shardCollection: "" + collA, key: {_id: 1}}));

    jsTestLog("Splitting up the collection...");

    // Split up the collection
    for (var i = 0; i < shards.length; i++) {
        assert.commandWorked(admin.runCommand({split: "" + collA, middle: {_id: i}}));
        assert.commandWorked(
            admin.runCommand({moveChunk: "" + collA, find: {_id: i}, to: shards[i].shardName}));
    }

    mongersB.getDB("admin").runCommand({flushRouterConfig: 1});
    mongersC.getDB("admin").runCommand({flushRouterConfig: 1});

    printjson(collB.count());
    printjson(collC.count());

    // Change up all the versions...
    for (var i = 0; i < shards.length; i++) {
        assert.commandWorked(admin.runCommand({
            moveChunk: "" + collA,
            find: {_id: i},
            to: shards[(i + 1) % shards.length].shardName
        }));
    }

    // Make sure mongers A is up-to-date
    mongersA.getDB("admin").runCommand({flushRouterConfig: 1});

    jsTestLog("Running count!");

    printjson(collB.count());
    printjson(collC.find().toArray());

    st.stop();

})();
