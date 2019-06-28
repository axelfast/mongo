//
// Tests mongers's failure tolerance for replica set shards and read preference queries
//
// Sets up a cluster with three shards, the first shard of which has an unsharded collection and
// half a sharded collection.  The second shard has the second half of the sharded collection, and
// the third shard has nothing.  Progressively shuts down the primary of each shard to see the
// impact on the cluster.
//
// Three different connection states are tested - active (connection is active through whole
// sequence), idle (connection is connected but not used before a shard change), and new
// (connection connected after shard change).
//

// Checking UUID consistency involves talking to shard primaries, but by the end of this test, one
// shard does not have a primary.
TestData.skipCheckingUUIDsConsistentAcrossCluster = true;

(function() {
    'use strict';

    var st = new ShardingTest({shards: 3, mongers: 1, other: {rs: true, rsOptions: {nodes: 2}}});

    var mongers = st.s0;
    var admin = mongers.getDB("admin");

    assert.commandWorked(admin.runCommand({setParameter: 1, traceExceptions: true}));

    var collSharded = mongers.getCollection("fooSharded.barSharded");
    var collUnsharded = mongers.getCollection("fooUnsharded.barUnsharded");

    // Create the unsharded database
    assert.writeOK(collUnsharded.insert({some: "doc"}));
    assert.writeOK(collUnsharded.remove({}));
    assert.commandWorked(
        admin.runCommand({movePrimary: collUnsharded.getDB().toString(), to: st.shard0.shardName}));

    // Create the sharded database
    assert.commandWorked(admin.runCommand({enableSharding: collSharded.getDB().toString()}));
    assert.commandWorked(
        admin.runCommand({movePrimary: collSharded.getDB().toString(), to: st.shard0.shardName}));
    assert.commandWorked(
        admin.runCommand({shardCollection: collSharded.toString(), key: {_id: 1}}));
    assert.commandWorked(admin.runCommand({split: collSharded.toString(), middle: {_id: 0}}));
    assert.commandWorked(admin.runCommand(
        {moveChunk: collSharded.toString(), find: {_id: 0}, to: st.shard1.shardName}));

    // Secondaries do not refresh their in-memory routing table until a request with a higher
    // version is received, and refreshing requires communication with the primary to obtain the
    // newest version. Read from the secondaries once before taking down primaries to ensure they
    // have loaded the routing table into memory.
    // TODO SERVER-30148: replace this with calls to awaitReplication() on each shard owning data
    // for the sharded collection once secondaries refresh proactively.
    var mongersSetupConn = new Monger(mongers.host);
    mongersSetupConn.setReadPref("secondary");
    assert(!mongersSetupConn.getCollection(collSharded.toString()).find({}).hasNext());

    gc();  // Clean up connections

    st.printShardingStatus();

    //
    // Setup is complete
    //

    jsTest.log("Inserting initial data...");

    var mongersConnActive = new Monger(mongers.host);
    var mongersConnIdle = null;
    var mongersConnNew = null;

    var wc = {writeConcern: {w: 2, wtimeout: 60000}};

    assert.writeOK(mongersConnActive.getCollection(collSharded.toString()).insert({_id: -1}, wc));
    assert.writeOK(mongersConnActive.getCollection(collSharded.toString()).insert({_id: 1}, wc));
    assert.writeOK(mongersConnActive.getCollection(collUnsharded.toString()).insert({_id: 1}, wc));

    jsTest.log("Stopping primary of third shard...");

    mongersConnIdle = new Monger(mongers.host);

    st.rs2.stop(st.rs2.getPrimary());

    jsTest.log("Testing active connection with third primary down...");

    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    assert.writeOK(mongersConnActive.getCollection(collSharded.toString()).insert({_id: -2}, wc));
    assert.writeOK(mongersConnActive.getCollection(collSharded.toString()).insert({_id: 2}, wc));
    assert.writeOK(mongersConnActive.getCollection(collUnsharded.toString()).insert({_id: 2}, wc));

    jsTest.log("Testing idle connection with third primary down...");

    assert.writeOK(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: -3}, wc));
    assert.writeOK(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: 3}, wc));
    assert.writeOK(mongersConnIdle.getCollection(collUnsharded.toString()).insert({_id: 3}, wc));

    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    jsTest.log("Testing new connections with third primary down...");

    mongersConnNew = new Monger(mongers.host);
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));
    mongersConnNew = new Monger(mongers.host);
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: 1}));
    mongersConnNew = new Monger(mongers.host);
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    mongersConnNew = new Monger(mongers.host);
    assert.writeOK(mongersConnNew.getCollection(collSharded.toString()).insert({_id: -4}, wc));
    mongersConnNew = new Monger(mongers.host);
    assert.writeOK(mongersConnNew.getCollection(collSharded.toString()).insert({_id: 4}, wc));
    mongersConnNew = new Monger(mongers.host);
    assert.writeOK(mongersConnNew.getCollection(collUnsharded.toString()).insert({_id: 4}, wc));

    gc();  // Clean up new connections

    jsTest.log("Stopping primary of second shard...");

    mongersConnIdle = new Monger(mongers.host);

    // Need to save this node for later
    var rs1Secondary = st.rs1.getSecondary();

    st.rs1.stop(st.rs1.getPrimary());

    jsTest.log("Testing active connection with second primary down...");

    // Reads with read prefs
    mongersConnActive.setSlaveOk();
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));
    mongersConnActive.setSlaveOk(false);

    mongersConnActive.setReadPref("primary");
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.throws(function() {
        mongersConnActive.getCollection(collSharded.toString()).findOne({_id: 1});
    });
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    // Ensure read prefs override slaveOK
    mongersConnActive.setSlaveOk();
    mongersConnActive.setReadPref("primary");
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.throws(function() {
        mongersConnActive.getCollection(collSharded.toString()).findOne({_id: 1});
    });
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));
    mongersConnActive.setSlaveOk(false);

    mongersConnActive.setReadPref("secondary");
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    mongersConnActive.setReadPref("primaryPreferred");
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    mongersConnActive.setReadPref("secondaryPreferred");
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    mongersConnActive.setReadPref("nearest");
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    // Writes
    assert.writeOK(mongersConnActive.getCollection(collSharded.toString()).insert({_id: -5}, wc));
    assert.writeError(mongersConnActive.getCollection(collSharded.toString()).insert({_id: 5}, wc));
    assert.writeOK(mongersConnActive.getCollection(collUnsharded.toString()).insert({_id: 5}, wc));

    jsTest.log("Testing idle connection with second primary down...");

    // Writes
    assert.writeOK(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: -6}, wc));
    assert.writeError(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: 6}, wc));
    assert.writeOK(mongersConnIdle.getCollection(collUnsharded.toString()).insert({_id: 6}, wc));

    // Reads with read prefs
    mongersConnIdle.setSlaveOk();
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));
    mongersConnIdle.setSlaveOk(false);

    mongersConnIdle.setReadPref("primary");
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.throws(function() {
        mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: 1});
    });
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    // Ensure read prefs override slaveOK
    mongersConnIdle.setSlaveOk();
    mongersConnIdle.setReadPref("primary");
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.throws(function() {
        mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: 1});
    });
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));
    mongersConnIdle.setSlaveOk(false);

    mongersConnIdle.setReadPref("secondary");
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    mongersConnIdle.setReadPref("primaryPreferred");
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    mongersConnIdle.setReadPref("secondaryPreferred");
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    mongersConnIdle.setReadPref("nearest");
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    jsTest.log("Testing new connections with second primary down...");

    // Reads with read prefs
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setSlaveOk();
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setSlaveOk();
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: 1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setSlaveOk();
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    gc();  // Clean up new connections incrementally to compensate for slow win32 machine.

    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("primary");
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("primary");
    assert.throws(function() {
        mongersConnNew.getCollection(collSharded.toString()).findOne({_id: 1});
    });
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("primary");
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    gc();  // Clean up new connections incrementally to compensate for slow win32 machine.

    // Ensure read prefs override slaveok
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setSlaveOk();
    mongersConnNew.setReadPref("primary");
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setSlaveOk();
    mongersConnNew.setReadPref("primary");
    assert.throws(function() {
        mongersConnNew.getCollection(collSharded.toString()).findOne({_id: 1});
    });
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setSlaveOk();
    mongersConnNew.setReadPref("primary");
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    gc();  // Clean up new connections incrementally to compensate for slow win32 machine.

    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("secondary");
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("secondary");
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: 1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("secondary");
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    gc();  // Clean up new connections incrementally to compensate for slow win32 machine.

    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("primaryPreferred");
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("primaryPreferred");
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: 1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("primaryPreferred");
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    gc();  // Clean up new connections incrementally to compensate for slow win32 machine.

    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("secondaryPreferred");
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("secondaryPreferred");
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: 1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("secondaryPreferred");
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    gc();  // Clean up new connections incrementally to compensate for slow win32 machine.

    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("nearest");
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("nearest");
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: 1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setReadPref("nearest");
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    gc();  // Clean up new connections incrementally to compensate for slow win32 machine.

    // Writes
    mongersConnNew = new Monger(mongers.host);
    assert.writeOK(mongersConnNew.getCollection(collSharded.toString()).insert({_id: -7}, wc));
    mongersConnNew = new Monger(mongers.host);
    assert.writeError(mongersConnNew.getCollection(collSharded.toString()).insert({_id: 7}, wc));
    mongersConnNew = new Monger(mongers.host);
    assert.writeOK(mongersConnNew.getCollection(collUnsharded.toString()).insert({_id: 7}, wc));

    gc();  // Clean up new connections

    jsTest.log("Stopping primary of first shard...");

    mongersConnIdle = new Monger(mongers.host);

    st.rs0.stop(st.rs0.getPrimary());

    jsTest.log("Testing active connection with first primary down...");

    mongersConnActive.setSlaveOk();
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    assert.writeError(mongersConnActive.getCollection(collSharded.toString()).insert({_id: -8}));
    assert.writeError(mongersConnActive.getCollection(collSharded.toString()).insert({_id: 8}));
    assert.writeError(mongersConnActive.getCollection(collUnsharded.toString()).insert({_id: 8}));

    jsTest.log("Testing idle connection with first primary down...");

    assert.writeError(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: -9}));
    assert.writeError(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: 9}));
    assert.writeError(mongersConnIdle.getCollection(collUnsharded.toString()).insert({_id: 9}));

    mongersConnIdle.setSlaveOk();
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: 1}));
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    jsTest.log("Testing new connections with first primary down...");

    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setSlaveOk();
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setSlaveOk();
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: 1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setSlaveOk();
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    mongersConnNew = new Monger(mongers.host);
    assert.writeError(mongersConnNew.getCollection(collSharded.toString()).insert({_id: -10}));
    mongersConnNew = new Monger(mongers.host);
    assert.writeError(mongersConnNew.getCollection(collSharded.toString()).insert({_id: 10}));
    mongersConnNew = new Monger(mongers.host);
    assert.writeError(mongersConnNew.getCollection(collUnsharded.toString()).insert({_id: 10}));

    gc();  // Clean up new connections

    jsTest.log("Stopping second shard...");

    mongersConnIdle = new Monger(mongers.host);

    st.rs1.stop(rs1Secondary);

    jsTest.log("Testing active connection with second shard down...");

    mongersConnActive.setSlaveOk();
    assert.neq(null, mongersConnActive.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnActive.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    assert.writeError(mongersConnActive.getCollection(collSharded.toString()).insert({_id: -11}));
    assert.writeError(mongersConnActive.getCollection(collSharded.toString()).insert({_id: 11}));
    assert.writeError(mongersConnActive.getCollection(collUnsharded.toString()).insert({_id: 11}));

    jsTest.log("Testing idle connection with second shard down...");

    assert.writeError(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: -12}));
    assert.writeError(mongersConnIdle.getCollection(collSharded.toString()).insert({_id: 12}));
    assert.writeError(mongersConnIdle.getCollection(collUnsharded.toString()).insert({_id: 12}));

    mongersConnIdle.setSlaveOk();
    assert.neq(null, mongersConnIdle.getCollection(collSharded.toString()).findOne({_id: -1}));
    assert.neq(null, mongersConnIdle.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    jsTest.log("Testing new connections with second shard down...");

    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setSlaveOk();
    assert.neq(null, mongersConnNew.getCollection(collSharded.toString()).findOne({_id: -1}));
    mongersConnNew = new Monger(mongers.host);
    mongersConnNew.setSlaveOk();
    assert.neq(null, mongersConnNew.getCollection(collUnsharded.toString()).findOne({_id: 1}));

    mongersConnNew = new Monger(mongers.host);
    assert.writeError(mongersConnNew.getCollection(collSharded.toString()).insert({_id: -13}));
    mongersConnNew = new Monger(mongers.host);
    assert.writeError(mongersConnNew.getCollection(collSharded.toString()).insert({_id: 13}));
    mongersConnNew = new Monger(mongers.host);
    assert.writeError(mongersConnNew.getCollection(collUnsharded.toString()).insert({_id: 13}));

    gc();  // Clean up new connections

    st.stop();

})();
