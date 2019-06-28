// Tests that mongers and shard mongerds can both be started up successfully when there is no config
// server, and that they will wait until there is a config server online before handling any
// sharding operations.
//
// This test involves restarting a standalone shard, so cannot be run on ephemeral storage engines.
// A restarted standalone will lose all data when using an ephemeral storage engine.
// @tags: [requires_persistence]

// The UUID consistency check uses connections to shards cached on the ShardingTest object, but this
// test restarts a shard, so the cached connection is not usable.
TestData.skipCheckingUUIDsConsistentAcrossCluster = true;

(function() {
    "use strict";

    /**
     * Restarts the mongerd backing the specified shard instance, without restarting the mongerbridge.
     */
    function restartShard(shard, waitForConnect) {
        MongerRunner.stopMongerd(shard);
        shard.restart = true;
        shard.waitForConnect = waitForConnect;
        MongerRunner.runMongerd(shard);
    }

    // TODO: SERVER-33830 remove shardAsReplicaSet: false
    var st = new ShardingTest({shards: 2, other: {shardAsReplicaSet: false}});

    jsTestLog("Setting up initial data");

    for (var i = 0; i < 100; i++) {
        assert.writeOK(st.s.getDB('test').foo.insert({_id: i}));
    }

    assert.commandWorked(st.s0.adminCommand({enableSharding: 'test'}));
    st.ensurePrimaryShard('test', st.shard0.shardName);

    assert.commandWorked(st.s0.adminCommand({shardCollection: 'test.foo', key: {_id: 1}}));
    assert.commandWorked(st.s0.adminCommand({split: 'test.foo', find: {_id: 50}}));
    assert.commandWorked(
        st.s0.adminCommand({moveChunk: 'test.foo', find: {_id: 75}, to: st.shard1.shardName}));

    // Make sure the pre-existing mongers already has the routing information loaded into memory
    assert.eq(100, st.s.getDB('test').foo.find().itcount());

    jsTestLog("Shutting down all config servers");
    for (var i = 0; i < st._configServers.length; i++) {
        st.stopConfigServer(i);
    }

    jsTestLog("Starting a new mongers when there are no config servers up");
    var newMongersInfo = MongerRunner.runMongers({configdb: st._configDB, waitForConnect: false});
    // The new mongers won't accept any new connections, but it should stay up and continue trying
    // to contact the config servers to finish startup.
    assert.throws(function() {
        new Monger(newMongersInfo.host);
    });

    jsTestLog("Restarting a shard while there are no config servers up");
    restartShard(st.shard1, false);

    jsTestLog("Queries should fail because the shard can't initialize sharding state");
    var error = assert.throws(function() {
        st.s.getDB('test').foo.find().itcount();
    });

    assert(ErrorCodes.ReplicaSetNotFound == error.code ||
           ErrorCodes.ExceededTimeLimit == error.code || ErrorCodes.HostUnreachable == error.code);

    jsTestLog("Restarting the config servers");
    for (var i = 0; i < st._configServers.length; i++) {
        st.restartConfigServer(i);
    }

    print("Sleeping for 60 seconds to let the other shards restart their ReplicaSetMonitors");
    sleep(60000);

    jsTestLog("Queries against the original mongers should work again");
    assert.eq(100, st.s.getDB('test').foo.find().itcount());

    jsTestLog("Should now be possible to connect to the mongers that was started while the config " +
              "servers were down");
    var newMongersConn = null;
    var caughtException = null;
    assert.soon(
        function() {
            try {
                newMongersConn = new Monger(newMongersInfo.host);
                return true;
            } catch (e) {
                caughtException = e;
                return false;
            }
        },
        "Failed to connect to mongers after config servers were restarted: " +
            tojson(caughtException));

    assert.eq(100, newMongersConn.getDB('test').foo.find().itcount());

    st.stop();
    MongerRunner.stopMongers(newMongersInfo);
}());
