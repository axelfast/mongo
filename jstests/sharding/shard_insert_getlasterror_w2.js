// replica set as solo shard
// TODO: Add assertion code that catches hang

// The UUID check must be able to contact the shard primaries, but this test manually stops 2/3
// nodes of a replica set.
TestData.skipCheckingUUIDsConsistentAcrossCluster = true;

(function() {
    "use strict";

    var numDocs = 2000;
    var baseName = "shard_insert_getlasterror_w2";
    var testDBName = baseName;
    var testCollName = 'coll';
    var replNodes = 3;

    // ~1KB string
    var textString = '';
    for (var i = 0; i < 40; i++) {
        textString += 'abcdefghijklmnopqrstuvwxyz';
    }

    // Spin up a sharded cluster, but do not add the shards
    var shardingTestConfig = {
        name: baseName,
        mongers: 1,
        shards: 1,
        rs: {nodes: replNodes},
        other: {manualAddShard: true}
    };
    var shardingTest = new ShardingTest(shardingTestConfig);

    // Get connection to the individual shard
    var replSet1 = shardingTest.rs0;

    // Add data to it
    var testDBReplSet1 = replSet1.getPrimary().getDB(testDBName);
    var bulk = testDBReplSet1.foo.initializeUnorderedBulkOp();
    for (var i = 0; i < numDocs; i++) {
        bulk.insert({x: i, text: textString});
    }
    assert.writeOK(bulk.execute());

    // Get connection to mongers for the cluster
    var mongersConn = shardingTest.s;
    var testDB = mongersConn.getDB(testDBName);

    // Add replSet1 as only shard
    assert.commandWorked(mongersConn.adminCommand({addshard: replSet1.getURL()}));

    // Enable sharding on test db and its collection foo
    assert.commandWorked(mongersConn.getDB('admin').runCommand({enablesharding: testDBName}));
    testDB[testCollName].ensureIndex({x: 1});
    assert.commandWorked(mongersConn.getDB('admin').runCommand(
        {shardcollection: testDBName + '.' + testCollName, key: {x: 1}}));

    // Test case where GLE should return an error
    assert.writeOK(testDB.foo.insert({_id: 'a', x: 1}));
    assert.writeError(testDB.foo.insert({_id: 'a', x: 1}, {writeConcern: {w: 2, wtimeout: 30000}}));

    // Add more data
    bulk = testDB.foo.initializeUnorderedBulkOp();
    for (var i = numDocs; i < 2 * numDocs; i++) {
        bulk.insert({x: i, text: textString});
    }
    assert.writeOK(bulk.execute({w: replNodes, wtimeout: 30000}));

    // Take down two nodes and make sure slaveOk reads still work
    var primary = replSet1._master;
    var secondary1 = replSet1._slaves[0];
    var secondary2 = replSet1._slaves[1];
    replSet1.stop(secondary1);
    replSet1.stop(secondary2);
    replSet1.waitForState(primary, ReplSetTest.State.SECONDARY);

    testDB.getMonger().adminCommand({setParameter: 1, logLevel: 1});
    testDB.getMonger().setSlaveOk();
    print("trying some queries");
    assert.soon(function() {
        try {
            testDB.foo.find().next();
        } catch (e) {
            print(e);
            return false;
        }
        return true;
    }, "Queries took too long to complete correctly.", 2 * 60 * 1000);

    // Shutdown cluster
    shardingTest.stop();

    print('shard_insert_getlasterror_w2.js SUCCESS');
})();
