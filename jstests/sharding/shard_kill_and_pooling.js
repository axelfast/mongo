/**
 * Tests what happens when a shard goes down with pooled connections.
 *
 * This test involves restarting a standalone shard, so cannot be run on ephemeral storage engines.
 * A restarted standalone will lose all data when using an ephemeral storage engine.
 * @tags: [requires_persistence]
 */

// Checking UUID consistency involves talking to a shard node, which in this test is shutdown
TestData.skipCheckingUUIDsConsistentAcrossCluster = true;

// Run through the same test twice, once with a hard -9 kill, once with a regular shutdown
(function() {
    'use strict';

    for (var test = 0; test < 2; test++) {
        var killWith = (test == 0 ? 15 : 9);

        var st = new ShardingTest({shards: 1});

        var mongers = st.s0;
        var coll = mongers.getCollection("foo.bar");
        var db = coll.getDB();

        assert.writeOK(coll.insert({hello: "world"}));

        jsTest.log("Creating new connections...");

        // Create a bunch of connections to the primary node through mongers.
        // jstest ->(x10)-> mongers ->(x10)-> primary
        var conns = [];
        for (var i = 0; i < 50; i++) {
            conns.push(new Mongo(mongers.host));
            assert.neq(null, conns[i].getCollection(coll + "").findOne());
        }

        jsTest.log("Returning the connections back to the pool.");

        for (var i = 0; i < conns.length; i++) {
            conns[i].close();
        }

        // Don't make test fragile by linking to format of shardConnPoolStats, but this is
        // useful if
        // something goes wrong.
        var connPoolStats = mongers.getDB("admin").runCommand({shardConnPoolStats: 1});
        printjson(connPoolStats);

        jsTest.log("Shutdown shard " + (killWith == 9 ? "uncleanly" : "") + "...");

        // Flush writes to disk, since sometimes we're killing uncleanly
        assert(mongers.getDB("admin").runCommand({fsync: 1}).ok);

        var exitCode = killWith === 9 ? MongoRunner.EXIT_SIGKILL : MongoRunner.EXIT_CLEAN;

        st.rs0.stopSet(killWith, true, {allowedExitCode: exitCode});

        jsTest.log("Restart shard...");
        st.rs0.startSet({forceLock: true}, true);

        jsTest.log("Waiting for socket timeout time...");

        // Need to wait longer than the socket polling time.
        sleep(2 * 5000);

        jsTest.log("Run queries using new connections.");

        var numErrors = 0;
        for (var i = 0; i < conns.length; i++) {
            var newConn = new Mongo(mongers.host);
            try {
                assert.neq(null, newConn.getCollection("foo.bar").findOne());
            } catch (e) {
                printjson(e);
                numErrors++;
            }
        }

        assert.eq(0, numErrors);

        st.stop();

        jsTest.log("DONE test " + test);
    }

})();
