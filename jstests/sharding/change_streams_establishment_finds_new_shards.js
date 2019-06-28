// Tests that change streams is able to find and return results from new shards which are added
// during cursor establishment.
// @tags: [uses_change_streams]
(function() {
    'use strict';

    // For supportsMajorityReadConcern().
    load("jstests/multiVersion/libs/causal_consistency_helpers.js");

    if (!supportsMajorityReadConcern()) {
        jsTestLog("Skipping test since storage engine doesn't support majority read concern.");
        return;
    }

    const rsNodeOptions = {
        // Use a higher frequency for periodic noops to speed up the test.
        setParameter: {periodicNoopIntervalSecs: 1, writePeriodicNoops: true}
    };
    const st =
        new ShardingTest({shards: 1, mongers: 1, rs: {nodes: 1}, other: {rsOptions: rsNodeOptions}});

    jsTestLog("Starting new shard (but not adding to shard set yet)");
    const newShard = new ReplSetTest({name: "newShard", nodes: 1, nodeOptions: rsNodeOptions});
    newShard.startSet({shardsvr: ''});
    newShard.initiate();

    const mongers = st.s;
    const mongersColl = mongers.getCollection('test.foo');
    const mongersDB = mongers.getDB("test");

    // Enable sharding to inform mongers of the database, allowing us to open a cursor.
    assert.commandWorked(mongers.adminCommand({enableSharding: mongersDB.getName()}));

    // Shard the collection.
    assert.commandWorked(
        mongers.adminCommand({shardCollection: mongersColl.getFullName(), key: {_id: 1}}));

    // Split the collection into two chunks: [MinKey, 10) and [10, MaxKey].
    assert.commandWorked(mongers.adminCommand({split: mongersColl.getFullName(), middle: {_id: 10}}));

    // Enable the failpoint.
    assert.commandWorked(mongers.adminCommand({
        configureFailPoint: "clusterAggregateHangBeforeEstablishingShardCursors",
        mode: "alwaysOn"
    }));

    // While opening the cursor, wait for the failpoint and add the new shard.
    const awaitNewShard = startParallelShell(`
        load("jstests/libs/check_log.js");
        checkLog.contains(db,
            "clusterAggregateHangBeforeEstablishingShardCursors fail point enabled");
        assert.commandWorked(
            db.adminCommand({addShard: "${newShard.getURL()}", name: "${newShard.name}"}));
        // Migrate the [10, MaxKey] chunk to "newShard".
        assert.commandWorked(db.adminCommand({moveChunk: "${mongersColl.getFullName()}",
                                              find: {_id: 20},
                                              to: "${newShard.name}",
                                              _waitForDelete: true}));
        assert.commandWorked(
            db.adminCommand(
                {configureFailPoint: "clusterAggregateHangBeforeEstablishingShardCursors",
                 mode: "off"}));`,
                                             mongers.port);

    jsTestLog("Opening $changeStream cursor");
    const changeStream = mongersColl.aggregate([{$changeStream: {}}]);
    assert(!changeStream.hasNext(), "Do not expect any results yet");

    // Clean up the parallel shell.
    awaitNewShard();

    // Insert two documents in different shards.
    assert.writeOK(mongersColl.insert({_id: 0}, {writeConcern: {w: "majority"}}));
    assert.writeOK(mongersColl.insert({_id: 20}, {writeConcern: {w: "majority"}}));

    // Expect to see them both.
    for (let id of[0, 20]) {
        jsTestLog("Expecting Item " + id);
        assert.soon(() => changeStream.hasNext());
        let next = changeStream.next();
        assert.eq(next.operationType, "insert");
        assert.eq(next.documentKey, {_id: id});
    }
    assert(!changeStream.hasNext());

    st.stop();
    newShard.stopSet();
})();
