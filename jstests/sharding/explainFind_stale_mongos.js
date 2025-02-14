/**
 * Tests that a stale mongers does not return a stale shardVersion error to the client for explain
 * find sent using the legacy query mode (it retries on the stale shardVersion error internally).
 */
(function() {
    "use strict";

    const dbName = "test";
    const collName = "foo";
    const ns = dbName + "." + collName;

    const st = new ShardingTest({mongers: 2, shards: 1, verbose: 2});

    let staleMongers = st.s0;
    let freshMongers = st.s1;

    jsTest.log("Make the stale mongers load a cache entry for db " + dbName + " once");
    assert.writeOK(staleMongers.getDB(dbName).getCollection(collName).insert({_id: 1}));

    jsTest.log("Call shardCollection on " + ns + " from the fresh mongers");
    assert.commandWorked(freshMongers.adminCommand({enableSharding: dbName}));
    assert.commandWorked(freshMongers.adminCommand({shardCollection: ns, key: {"_id": 1}}));

    jsTest.log("Ensure the shard knows " + ns + " is sharded");
    assert.commandWorked(
        st.shard0.adminCommand({_flushRoutingTableCacheUpdates: ns, syncFromConfig: true}));

    jsTest.log("Run explain find on " + ns + " from the stale mongers");
    staleMongers.getDB(dbName).getMonger().forceReadMode("legacy");
    staleMongers.getDB(dbName).getCollection(collName).find({$query: {}, $explain: true}).next();

    st.stop();
})();
