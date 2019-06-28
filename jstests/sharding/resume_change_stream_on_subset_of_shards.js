// This tests resuming a change stream on a sharded collection where not all shards have a chunk in
// the collection.
// @tags: [uses_change_streams]
(function() {
    "use strict";

    // For supportsMajorityReadConcern().
    load("jstests/multiVersion/libs/causal_consistency_helpers.js");

    if (!supportsMajorityReadConcern()) {
        jsTestLog("Skipping test since storage engine doesn't support majority read concern.");
        return;
    }

    // Create a 3-shard cluster. Enable 'writePeriodicNoops' and set 'periodicNoopIntervalSecs' to 1
    // second so that each shard is continually advancing its optime, allowing the
    // AsyncResultsMerger to return sorted results even if some shards have not yet produced any
    // data.
    const st = new ShardingTest({
        shards: 3,
        rs: {nodes: 1, setParameter: {writePeriodicNoops: true, periodicNoopIntervalSecs: 1}}
    });

    const mongersDB = st.s0.getDB(jsTestName());
    const mongersColl = mongersDB.test;

    // Enable sharding on the test DB and ensure its primary is shard 0.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.rs0.getURL());

    // Shard the test collection on _id, split the collection into 2 chunks: [MinKey, 0) and
    // [0, MaxKey), then move the [0, MaxKey) chunk to shard 1.
    assert.commandWorked(
        mongersDB.adminCommand({shardCollection: mongersColl.getFullName(), key: {_id: 1}}));
    assert.commandWorked(
        mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {_id: 0}}));
    assert.commandWorked(mongersDB.adminCommand(
        {moveChunk: mongersColl.getFullName(), find: {_id: 1}, to: st.rs1.getURL()}));

    // Establish a change stream...
    let changeStream = mongersColl.watch();

    // ... then do one write to produce a resume token...
    assert.writeOK(mongersColl.insert({_id: -2}));
    assert.soon(() => changeStream.hasNext());
    const resumeToken = changeStream.next()._id;

    // ... followed by one write to each chunk for testing purposes, i.e. shards 0 and 1.
    assert.writeOK(mongersColl.insert({_id: -1}));
    assert.writeOK(mongersColl.insert({_id: 1}));

    // The change stream should see all the inserts after establishing cursors on all shards.
    for (let nextId of[-1, 1]) {
        assert.soon(() => changeStream.hasNext());
        let next = changeStream.next();
        assert.eq(next.operationType, "insert");
        assert.eq(next.fullDocument, {_id: nextId});
        jsTestLog(`Saw insert for _id ${nextId}`);
    }

    // Insert another document after storing the resume token.
    assert.writeOK(mongersColl.insert({_id: 2}));

    // Resume the change stream and verify that it correctly sees the next insert.  This is meant
    // to test resuming a change stream when not all shards are aware that the collection exists,
    // since shard 2 has no data at this point.
    changeStream = mongersColl.watch([], {resumeAfter: resumeToken});
    assert.soon(() => changeStream.hasNext());
    let next = changeStream.next();
    assert.eq(next.documentKey, {_id: -1});
    assert.eq(next.fullDocument, {_id: -1});
    assert.eq(next.operationType, "insert");

    st.stop();
}());
