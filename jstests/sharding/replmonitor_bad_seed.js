/**
 * This tests tries to check that a ReplicaSetMonitor initialized with a
 * replica set seed that has none of the nodes up will be able to recover
 * once the replica set come back up.
 *
 * ReplSetMonitor is tested indirectly through mongers. This is because
 * attempting to create a connection through the Monger constructor won't
 * work because the shell will throw an exception before the monger shell
 * binds the variable properly to the js environment (in simple terms,
 * the connection object is never returned when it cannot connect to it).
 * Another reason for using mongers in this test is so we can use
 * connPoolStats to synchronize the test and make sure that the monitor
 * was able to refresh before proceeding to check.
 *
 * Any tests that restart a shard mongerd and send sharding requests to it after restart cannot make
 * the shard use an in-memory storage engine, since the shardIdentity document will be lost after
 * restart.
 *
 * @tags: [requires_persistence]
 */
(function() {
    'use strict';
    load("jstests/replsets/rslib.js");

    var st = new ShardingTest({shards: 1, rs: {oplogSize: 10}});
    var replTest = st.rs0;

    assert.commandWorked(st.s0.adminCommand({enableSharding: 'test'}));
    assert.commandWorked(st.s0.adminCommand({shardCollection: 'test.user', key: {x: 1}}));

    // The cluster now has the shard information. Then kill the replica set so when mongers restarts
    // and tries to create a ReplSetMonitor for that shard, it will not be able to connect to any of
    // the seed servers.
    // Don't clear the data directory so that the shardIdentity is not deleted.
    replTest.stopSet(undefined /* send default signal */, true /* don't clear data directory */);

    st.restartMongers(0);

    replTest.startSet({restart: true, noCleanData: true});
    replTest.awaitSecondaryNodes();

    // Verify that the replSetMonitor can reach the restarted set
    awaitRSClientHosts(st.s0, replTest.nodes, {ok: true});
    replTest.awaitNodesAgreeOnPrimary();

    assert.writeOK(st.s0.getDB('test').user.insert({x: 1}));

    st.stop();
})();
