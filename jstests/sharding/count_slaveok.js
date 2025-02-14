/**
 * Tests count and distinct using slaveOk. Also tests a scenario querying a set where only one
 * secondary is up.
 */

// Checking UUID consistency involves talking to a shard node, which in this test is shutdown
TestData.skipCheckingUUIDsConsistentAcrossCluster = true;

(function() {
    'use strict';

    load("jstests/replsets/rslib.js");

    var st = new ShardingTest({shards: 1, mongers: 1, other: {rs: true, rs0: {nodes: 2}}});
    var rst = st.rs0;

    // Insert data into replica set
    var conn = new Monger(st.s.host);

    var coll = conn.getCollection('test.countSlaveOk');
    coll.drop();

    var bulk = coll.initializeUnorderedBulkOp();
    for (var i = 0; i < 300; i++) {
        bulk.insert({i: i % 10});
    }
    assert.writeOK(bulk.execute());

    var connA = conn;
    var connB = new Monger(st.s.host);
    var connC = new Monger(st.s.host);

    st.printShardingStatus();

    // Wait for client to update itself and replication to finish
    rst.awaitReplication();

    var primary = rst.getPrimary();
    var sec = rst.getSecondary();

    // Data now inserted... stop the master, since only two in set, other will still be secondary
    rst.stop(rst.getPrimary());
    printjson(rst.status());

    // Wait for the mongers to recognize the slave
    awaitRSClientHosts(conn, sec, {ok: true, secondary: true});

    // Make sure that mongers realizes that primary is already down
    awaitRSClientHosts(conn, primary, {ok: false});

    // Need to check slaveOk=true first, since slaveOk=false will destroy conn in pool when
    // master is down
    conn.setSlaveOk();

    // count using the command path
    assert.eq(30, coll.find({i: 0}).count());
    // count using the query path
    assert.eq(30, coll.find({i: 0}).itcount());
    assert.eq(10, coll.distinct("i").length);

    try {
        conn.setSlaveOk(false);
        // Should throw exception, since not slaveOk'd
        coll.find({i: 0}).count();

        print("Should not reach here!");
        assert(false);
    } catch (e) {
        print("Non-slaveOk'd connection failed.");
    }

    st.stop();
})();
