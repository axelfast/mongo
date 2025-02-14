/**
 * This tests whether slaveOk reads are properly routed through mongers in
 * an authenticated environment. This test also includes restarting the
 * entire set, then querying afterwards.
 *
 * This test involves a full restart of the replica set, so cannot be run with ephemeral storage
 * engines. When all nodes in a replica set are using an ephemeral storage engine, the set cannot
 * recover from a full restart. Once restarted, the nodes will have no knowledge of the replica set
 * config and will be unable to elect a primary.
 * @tags: [requires_persistence, requires_find_command]
 */
(function() {
    'use strict';
    load("jstests/replsets/rslib.js");

    // Replica set nodes started with --shardsvr do not enable key generation until they are added
    // to a sharded cluster and reject commands with gossiped clusterTime from users without the
    // advanceClusterTime privilege. This causes ShardingTest setup to fail because the shell
    // briefly authenticates as __system and recieves clusterTime metadata then will fail trying to
    // gossip that time later in setup.
    //
    // TODO SERVER-32672: remove this flag.
    TestData.skipGossipingClusterTime = true;

    /**
     * Checks if a query to the given collection will be routed to the secondary. Returns true if
     * query was routed to a secondary node.
     */
    function doesRouteToSec(coll, query) {
        var explain = coll.find(query).explain();
        assert.eq("SINGLE_SHARD", explain.queryPlanner.winningPlan.stage);
        var serverInfo = explain.queryPlanner.winningPlan.shards[0].serverInfo;
        var conn = new Monger(serverInfo.host + ":" + serverInfo.port.toString());
        var cmdRes = conn.getDB('admin').runCommand({isMaster: 1});

        jsTest.log('isMaster: ' + tojson(cmdRes));

        return cmdRes.secondary;
    }

    var rsOpts = {oplogSize: 50};
    // TODO: Remove 'shardAsReplicaSet: false' when SERVER-32672 is fixed.
    var st = new ShardingTest(
        {shards: 1, rs: rsOpts, other: {keyFile: 'jstests/libs/key1', shardAsReplicaSet: false}});

    var mongers = st.s;
    var replTest = st.rs0;
    var testDB = mongers.getDB('AAAAA');
    var coll = testDB.user;
    var nodeCount = replTest.nodes.length;

    /* Add an admin user to the replica member to simulate connecting from
     * remote location. This is because mongerd allows unautheticated
     * connections to access the server from localhost connections if there
     * is no admin user.
     */
    var adminDB = mongers.getDB('admin');
    adminDB.createUser({user: 'user', pwd: 'password', roles: jsTest.adminUserRoles});
    adminDB.auth('user', 'password');
    var priAdminDB = replTest.getPrimary().getDB('admin');
    replTest.getPrimary().waitForClusterTime(60);
    priAdminDB.createUser({user: 'user', pwd: 'password', roles: jsTest.adminUserRoles},
                          {w: 3, wtimeout: 30000});

    coll.drop();
    coll.setSlaveOk(true);

    /* Secondaries should be up here, but they can still be in RECOVERY
     * state, which will make the ReplicaSetMonitor mark them as
     * ok = false and not eligible for slaveOk queries.
     */
    awaitRSClientHosts(mongers, replTest.getSecondaries(), {ok: true, secondary: true});

    var bulk = coll.initializeUnorderedBulkOp();
    for (var x = 0; x < 20; x++) {
        bulk.insert({v: x, k: 10});
    }
    assert.writeOK(bulk.execute({w: nodeCount}));

    /* Although mongers never caches query results, try to do a different query
     * everytime just to be sure.
     */
    var vToFind = 0;

    jsTest.log('First query to SEC');
    assert(doesRouteToSec(coll, {v: vToFind++}));

    var SIG_TERM = 15;
    replTest.stopSet(SIG_TERM, true, {auth: {user: 'user', pwd: 'password'}});

    for (var n = 0; n < nodeCount; n++) {
        replTest.restart(n, rsOpts);
    }

    replTest.awaitSecondaryNodes();

    coll.setSlaveOk(true);

    /* replSetMonitor does not refresh the nodes information when getting secondaries.
     * A node that is previously labeled as secondary can now be a primary, so we
     * wait for the replSetMonitorWatcher thread to refresh the nodes information.
     */
    awaitRSClientHosts(mongers, replTest.getSecondaries(), {ok: true, secondary: true});
    //
    // We also need to wait for the primary, it's possible that the mongers may think a node is a
    // secondary but it actually changed to a primary before we send our final query.
    //
    awaitRSClientHosts(mongers, replTest.getPrimary(), {ok: true, ismaster: true});

    // Recheck if we can still query secondaries after refreshing connections.
    jsTest.log('Final query to SEC');
    assert(doesRouteToSec(coll, {v: vToFind++}));

    // Cleanup auth so Windows will be able to shutdown gracefully
    priAdminDB = replTest.getPrimary().getDB('admin');
    priAdminDB.auth('user', 'password');
    priAdminDB.dropUser('user');

    st.stop();
})();
