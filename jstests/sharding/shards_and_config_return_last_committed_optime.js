/**
 * Tests that lastCommittedOpTime is returned in all command responses from:
 *   - sharding aware shards servers (primary + secondary)
 *   - config servers (primary + secondary)
 *
 * And is not returned by:
 *   - mongers
 *   - non-sharding aware shard servers (primary + secondary)
 *   - mongerd from a standalone replica set (primary + secondary)
 *   - standalone mongerd
 */
(function() {
    "use strict";

    load("jstests/libs/write_concern_util.js");  // For stopReplProducer

    function assertCmdDoesNotReturnLastCommittedOpTime(testDB, cmdObj, connType, expectSuccess) {
        const res = testDB.runCommand(cmdObj);
        assert.eq(expectSuccess ? 1 : 0, res.ok);
        assert(typeof res.lastCommittedOpTime === "undefined",
               "Expected response from a " + connType + " to not contain lastCommittedOpTime," +
                   " received: " + tojson(res) + ", cmd was: " + tojson(cmdObj));
    }

    function assertDoesNotReturnLastCommittedOpTime(testDB, collName, connType) {
        // Successful commands return lastCommittedOpTime.
        assertCmdDoesNotReturnLastCommittedOpTime(testDB, {find: collName}, connType, true);

        // Failed commands return lastCommittedOpTime.
        assertCmdDoesNotReturnLastCommittedOpTime(
            testDB, {dummyCommand: collName}, connType, false /* expectSuccess */);
        assertCmdDoesNotReturnLastCommittedOpTime(testDB,
                                                  {find: collName, readConcern: {invalid: "rc"}},
                                                  connType,
                                                  false /* expectSuccess */);
        assertCmdDoesNotReturnLastCommittedOpTime(
            testDB,
            {insert: collName, documents: [{x: 2}], writeConcern: {invalid: "wc"}},
            connType,
            false /* expectSuccess */);
    }

    function assertCmdReturnsLastCommittedOpTime(testDB, cmdObj, connType, expectSuccess) {
        // Retry up to one time to avoid possible failures from lag in setting the
        // lastCommittedOpTime.
        assert.retryNoExcept(() => {
            const res = testDB.runCommand(cmdObj);
            assert.eq(expectSuccess ? 1 : 0, res.ok);
            assert(typeof res.lastCommittedOpTime !== "undefined",
                   "Expected response from a " + connType + " to contain lastCommittedOpTime," +
                       " received: " + tojson(res) + ", cmd was: " + tojson(cmdObj));

            const statusRes = assert.commandWorked(testDB.adminCommand({replSetGetStatus: 1}));
            assert.eq(
                0,
                bsonWoCompare(res.lastCommittedOpTime, statusRes.optimes.lastCommittedOpTime.ts),
                "lastCommittedOpTime in command response, " + res.lastCommittedOpTime +
                    ", does not equal the replSetGetStatus lastCommittedOpTime timestamp, " +
                    statusRes.optimes.lastCommittedOpTime.ts + ", cmd was: " + tojson(cmdObj));

            return true;
        }, "command: " + tojson(cmdObj) + " failed to return correct lastCommittedOpTime", 2);
    }

    function assertReturnsLastCommittedOpTime(testDB, collName, connType) {
        // Successful commands return lastCommittedOpTime.
        assertCmdReturnsLastCommittedOpTime(
            testDB, {find: collName}, connType, true /* expectSuccess */);

        // Failed commands return lastCommittedOpTime.
        assertCmdReturnsLastCommittedOpTime(
            testDB, {dummyCommand: collName}, connType, false /* expectSuccess */);
        assertCmdReturnsLastCommittedOpTime(testDB,
                                            {find: collName, readConcern: {invalid: "rc"}},
                                            connType,
                                            false /* expectSuccess */);
        assertCmdReturnsLastCommittedOpTime(
            testDB,
            {insert: collName, documents: [{x: 2}], writeConcern: {invalid: "wc"}},
            connType,
            false /* expectSuccess */);
    }

    //
    // Mongers should not return lastCommittedOpTime.
    //

    const st = new ShardingTest({shards: 1, rs: {nodes: 2}, config: 2});
    assert.commandWorked(st.s.adminCommand({enableSharding: "test"}));
    assert.commandWorked(st.s.adminCommand({shardCollection: "test.foo", key: {x: 1}}));

    // Sharded collection.
    assertDoesNotReturnLastCommittedOpTime(
        st.s.getDB("test"), "foo", "mongers talking to a sharded collection");

    // Unsharded collection.
    assertDoesNotReturnLastCommittedOpTime(
        st.s.getDB("test"), "unsharded", "mongers talking to a non-sharded collection");

    // Collection stored on the config server.
    assertDoesNotReturnLastCommittedOpTime(
        st.s.getDB("config"), "foo", "mongers talking to a config server collection");

    //
    // A mongerd in a sharded replica set returns lastCommittedOpTime.
    //

    // To verify the lastCommittedOpTime is being returned, pause replication on the secondary to
    // prevent the primary from advancing its lastCommittedOpTime and then perform a local write to
    // advance the primary's lastAppliedOpTime.
    let primary = st.rs0.getPrimary();
    let secondary = st.rs0.getSecondary();

    st.rs0.awaitLastOpCommitted();
    stopServerReplication(secondary);
    assert.writeOK(primary.getDB("test").foo.insert({x: 1}, {writeConcern: {w: 1}}));

    // Sharded collection.
    assertReturnsLastCommittedOpTime(primary.getDB("test"), "foo", "sharding-aware shard primary");
    assertReturnsLastCommittedOpTime(
        secondary.getDB("test"), "foo", "sharding-aware shard secondary");

    // Unsharded collection.
    assertReturnsLastCommittedOpTime(
        primary.getDB("test"), "unsharded", "sharding-aware shard primary");
    assertReturnsLastCommittedOpTime(
        secondary.getDB("test"), "unsharded", "sharding-aware shard secondary");

    restartServerReplication(secondary);

    //
    // A config server in a sharded replica set returns lastCommittedOpTime.
    //

    // Split the lastCommitted and lastApplied opTimes by pausing secondary application and
    // performing a local write.
    primary = st.configRS.getPrimary();
    secondary = st.configRS.getSecondary();

    st.configRS.awaitLastOpCommitted();
    stopServerReplication(secondary);
    assert.writeOK(primary.getDB("config").foo.insert({x: 1}, {writeConcern: {w: 1}}));

    assertReturnsLastCommittedOpTime(primary.getDB("test"), "foo", "config server primary");
    assertReturnsLastCommittedOpTime(secondary.getDB("test"), "foo", "config server secondary");

    restartServerReplication(secondary);
    st.stop();

    //
    // A mongerd started with --shardsvr that is not sharding aware does not return
    // lastCommittedOpTime.
    //

    const replTestShardSvr = new ReplSetTest({nodes: 2, nodeOptions: {shardsvr: ""}});
    replTestShardSvr.startSet();
    replTestShardSvr.initiate();

    assertDoesNotReturnLastCommittedOpTime(
        replTestShardSvr.getPrimary().getDB("test"), "foo", "non-sharding aware shard primary");
    assertDoesNotReturnLastCommittedOpTime(
        replTestShardSvr.getSecondary().getDB("test"), "foo", "non-sharding aware shard secondary");

    replTestShardSvr.stopSet();

    //
    // A mongerd from a standalone replica set does not return lastCommittedOpTime.
    //

    const replTest = new ReplSetTest({nodes: 2});
    replTest.startSet();
    replTest.initiate();

    assertDoesNotReturnLastCommittedOpTime(
        replTest.getPrimary().getDB("test"), "foo", "standalone replica set primary");
    assertDoesNotReturnLastCommittedOpTime(
        replTest.getSecondary().getDB("test"), "foo", "standalone replica set secondary");

    replTest.stopSet();

    //
    // A standalone mongerd does not return lastCommittedOpTime.
    //

    const standalone = MongerRunner.runMongerd();

    assertDoesNotReturnLastCommittedOpTime(standalone.getDB("test"), "foo", "standalone mongerd");

    MongerRunner.stopMongerd(standalone);
})();
