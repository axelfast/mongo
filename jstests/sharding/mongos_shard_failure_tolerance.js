//
// Tests mongers's failure tolerance for single-node shards
//
// Sets up a cluster with three shards, the first shard of which has an unsharded collection and
// half a sharded collection.  The second shard has the second half of the sharded collection, and
// the third shard has nothing.  Progressively shuts down each shard to see the impact on the
// cluster.
//
// Three different connection states are tested - active (connection is active through whole
// sequence), idle (connection is connected but not used before a shard change), and new
// (connection connected after shard change).
//
// Checking UUID consistency involves talking to shards, but this test shuts down shards.
TestData.skipCheckingUUIDsConsistentAcrossCluster = true;

(function() {
    'use strict';

    var st = new ShardingTest({shards: 3, mongers: 1});

    var admin = st.s0.getDB("admin");

    var collSharded = st.s0.getCollection("fooSharded.barSharded");
    var collUnsharded = st.s0.getCollection("fooUnsharded.barUnsharded");

    assert.commandWorked(admin.runCommand({enableSharding: collSharded.getDB().toString()}));
    st.ensurePrimaryShard(collSharded.getDB().toString(), st.shard0.shardName);

    assert.commandWorked(
        admin.runCommand({shardCollection: collSharded.toString(), key: {_id: 1}}));
    assert.commandWorked(admin.runCommand({split: collSharded.toString(), middle: {_id: 0}}));
    assert.commandWorked(admin.runCommand(
        {moveChunk: collSharded.toString(), find: {_id: 0}, to: st.shard1.shardName}));

    // Create the unsharded database
    assert.writeOK(collUnsharded.insert({some: "doc"}));
    assert.writeOK(collUnsharded.remove({}));
    st.ensurePrimaryShard(collUnsharded.getDB().toString(), st.shard0.shardName);

    //
    // Setup is complete
    //

    jsTest.log("Inserting initial data...");

    var mongersConnActive = new Mongo(st.s0.host);
    var mongersConnIdle = null;
    var mongersConnNew = null;

    assert.writeOK(mongersConnActive.getCollection(collSharded.toString()).insert({_id: -1}));
    assert.writeOK(mongersConnActive.getCollection(collSharded.toString()).insert({_id: 1}));
    assert.writeOK(mongersConnActive.getCollection(collUnsharded.toString()).insert({_id: 1}));

    jsTest.log("Stopping third shard...");

    mongersConnIdle = new Mongo(st.s0.host);

    st.rs2.stopSet();

    jsTest.log("Testing active connection...");

    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    assert.writeOK(mongersConnActive.getCollection(collSharded.toString()).insert({_id: -2}));
    assert.writeOK(mongersConnActive.getCollection(collSharded.toString()).insert({_id: 2}));
    assert.writeOK(mongersConnActive.getCollection(collUnsharded.toString()).insert({_id: 2}));

    jsTest.log("Testing idle connection...");

    assert.writeOK(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: -3}));
    assert.writeOK(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: 3}));
    assert.writeOK(mongersConnIdle.getCollection(collUnsharded.toString()).insert({_id: 3}));

    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    jsTest.log("Testing new connections...");

    mongersConnNew = new Mongo(st.s0.host);
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));
    mongersConnNew = new Mongo(st.s0.host);
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: 1}));
    mongersConnNew = new Mongo(st.s0.host);
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    mongersConnNew = new Mongo(st.s0.host);
    assert.writeOK(mongersConnNew.getCollection(collSharded.toString()).insert({_id: -4}));
    mongersConnNew = new Mongo(st.s0.host);
    assert.writeOK(mongersConnNew.getCollection(collSharded.toString()).insert({_id: 4}));
    mongersConnNew = new Mongo(st.s0.host);
    assert.writeOK(mongersConnNew.getCollection(collUnsharded.toString()).insert({_id: 4}));

    gc();  // Clean up new connections

    jsTest.log("Stopping second shard...");

    mongersConnIdle = new Mongo(st.s0.host);

    st.rs1.stopSet();
    jsTest.log("Testing active connection...");

    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    assert.writeOK(mongersConnActive.getCollection(collSharded.toString()).insert({_id: -5}));

    assert.writeError(mongersConnActive.getCollection(collSharded.toString()).insert({_id: 5}));
    assert.writeOK(mongersConnActive.getCollection(collUnsharded.toString()).insert({_id: 5}));

    jsTest.log("Testing idle connection...");

    assert.writeOK(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: -6}));
    assert.writeError(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: 6}));
    assert.writeOK(mongersConnIdle.getCollection(collUnsharded.toString()).insert({_id: 6}));

    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    jsTest.log("Testing new connections...");

    mongersConnNew = new Mongo(st.s0.host);
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));

    mongersConnNew = new Mongo(st.s0.host);
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    mongersConnNew = new Mongo(st.s0.host);
    assert.writeOK(mongersConnNew.getCollection(collSharded.toString()).insert({_id: -7}));

    mongersConnNew = new Mongo(st.s0.host);
    assert.writeError(mongersConnNew.getCollection(collSharded.toString()).insert({_id: 7}));

    mongersConnNew = new Mongo(st.s0.host);
    assert.writeOK(mongersConnNew.getCollection(collUnsharded.toString()).insert({_id: 7}));

    st.stop();

})();
