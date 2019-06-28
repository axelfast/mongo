// This test ensures that change streams on sharded collections start in sync with each other.
//
// As detailed in SERVER-31685, since shard cursors are not established simultaneously, it is
// possible that a sharded change stream could be established on shard 0, then write 'A' to shard 0
// could occur, followed by write 'B' to shard 1, and then the change stream could be established on
// shard 1, then some third write 'C' could occur.  This test ensures that in that case, both 'A'
// and 'B' will be seen in the changestream before 'C'.
// @tags: [uses_change_streams]
(function() {
    "use strict";

    // For supportsMajorityReadConcern().
    load("jstests/multiVersion/libs/causal_consistency_helpers.js");

    if (!supportsMajorityReadConcern()) {
        jsTestLog("Skipping test since storage engine doesn't support majority read concern.");
        return;
    }

    const st = new ShardingTest({
        shards: 2,
        mongers: 2,
        useBridge: true,
        rs: {
            nodes: 1,
            // Use a higher frequency for periodic noops to speed up the test.
            setParameter: {writePeriodicNoops: true, periodicNoopIntervalSecs: 1}
        }
    });

    const mongersDB = st.s0.getDB(jsTestName());
    const mongersColl = mongersDB[jsTestName()];

    // Enable sharding on the test DB and ensure its primary is st.shard0.shardName.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.rs0.getURL());

    // Shard the test collection on _id.
    assert.commandWorked(
        mongersDB.adminCommand({shardCollection: mongersColl.getFullName(), key: {_id: 1}}));

    // Split the collection into 2 chunks: [MinKey, 0), [0, MaxKey).
    assert.commandWorked(
        mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {_id: 0}}));

    // Move the [0, MaxKey) chunk to st.shard1.shardName.
    assert.commandWorked(mongersDB.adminCommand(
        {moveChunk: mongersColl.getFullName(), find: {_id: 1}, to: st.rs1.getURL()}));

    function checkStream() {
        load('jstests/libs/change_stream_util.js');  // For assertChangeStreamEventEq.

        db = db.getSiblingDB(jsTestName());
        let coll = db[jsTestName()];
        let changeStream = coll.aggregate([{$changeStream: {}}]);

        assert.soon(() => changeStream.hasNext());
        assertChangeStreamEventEq(changeStream.next(), {
            documentKey: {_id: -1000},
            fullDocument: {_id: -1000},
            ns: {db: db.getName(), coll: coll.getName()},
            operationType: "insert",
        });

        assert.soon(() => changeStream.hasNext());
        assertChangeStreamEventEq(changeStream.next(), {
            documentKey: {_id: 1001},
            fullDocument: {_id: 1001},
            ns: {db: db.getName(), coll: coll.getName()},
            operationType: "insert",
        });

        assert.soon(() => changeStream.hasNext());
        assertChangeStreamEventEq(changeStream.next(), {
            documentKey: {_id: -1002},
            fullDocument: {_id: -1002},
            ns: {db: db.getName(), coll: coll.getName()},
            operationType: "insert",
        });
        changeStream.close();
    }

    // Start the $changeStream with shard 1 unavailable on the second mongers (s1).  We will be
    // writing through the first mongers (s0), which will remain connected to all shards.
    st.rs1.getPrimary().disconnect(st.s1);
    let waitForShell = startParallelShell(checkStream, st.s1.port);

    // Wait for the aggregate cursor to appear in currentOp on the current shard.
    function waitForShardCursor(rs) {
        assert.soon(() => st.rs0.getPrimary()
                              .getDB('admin')
                              .aggregate([
                                  {"$currentOp": {"idleCursors": true}},
                                  {"$match": {ns: mongersColl.getFullName(), type: "idleCursor"}}

                              ])
                              .itcount() === 1);
    }
    // Make sure the shard 0 $changeStream cursor is established before doing the first writes.
    waitForShardCursor(st.rs0);

    assert.writeOK(mongersColl.insert({_id: -1000}, {writeConcern: {w: "majority"}}));

    // This write to shard 1 occurs before the $changeStream cursor on shard 1 is open, because the
    // mongers where the $changeStream is running is disconnected from shard 1.
    assert.writeOK(mongersColl.insert({_id: 1001}, {writeConcern: {w: "majority"}}));

    jsTestLog("Reconnecting");
    st.rs1.getPrimary().reconnect(st.s1);
    waitForShardCursor(st.rs1);

    assert.writeOK(mongersColl.insert({_id: -1002}, {writeConcern: {w: "majority"}}));
    waitForShell();
    st.stop();
})();
