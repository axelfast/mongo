// Test write re-routing on version mismatch.
(function() {
    'use strict';

    var st = new ShardingTest({shards: 2, mongers: 2});

    var mongers = st.s;
    var admin = mongers.getDB("admin");
    var config = mongers.getDB("config");
    var coll = st.s.getCollection('TestDB.coll');

    assert.commandWorked(mongers.adminCommand({enableSharding: 'TestDB'}));
    st.ensurePrimaryShard('TestDB', st.shard0.shardName);
    assert.commandWorked(mongers.adminCommand({shardCollection: 'TestDB.coll', key: {_id: 1}}));

    jsTest.log("Refreshing second mongers...");

    var mongersB = st.s1;
    var adminB = mongersB.getDB("admin");
    var collB = mongersB.getCollection(coll + "");

    // Make sure mongersB knows about the coll
    assert.eq(0, collB.find().itcount());

    jsTest.log("Moving chunk to create stale mongers...");
    assert.commandWorked(
        admin.runCommand({moveChunk: coll + "", find: {_id: 0}, to: st.shard1.shardName}));

    jsTest.log("Inserting docs that needs to be retried...");

    var nextId = -1;
    for (var i = 0; i < 2; i++) {
        printjson("Inserting " + nextId);
        assert.writeOK(collB.insert({_id: nextId--, hello: "world"}));
    }

    jsTest.log("Inserting doc which successfully goes through...");

    // Do second write
    assert.writeOK(collB.insert({_id: nextId--, goodbye: "world"}));

    // Assert that write went through
    assert.eq(coll.find().itcount(), 3);

    jsTest.log("Now try moving the actual chunk we're writing to...");

    // Now move the actual chunk we're writing to
    printjson(admin.runCommand({moveChunk: coll + "", find: {_id: -1}, to: st.shard1.shardName}));

    jsTest.log("Inserting second docs to get written back...");

    // Will fail entirely if too many of these, waiting for write to get applied can get too long.
    for (var i = 0; i < 2; i++) {
        collB.insert({_id: nextId--, hello: "world"});
    }

    // Refresh server
    printjson(adminB.runCommand({flushRouterConfig: 1}));

    jsTest.log("Inserting second doc which successfully goes through...");

    // Do second write
    assert.writeOK(collB.insert({_id: nextId--, goodbye: "world"}));

    jsTest.log("All docs written this time!");

    // Assert that writes went through.
    assert.eq(coll.find().itcount(), 6);

    st.stop();
})();
