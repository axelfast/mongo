// Tests metadata notifications of change streams on sharded collections.
// Legacy getMore fails after dropping the database that the original cursor is on.
// @tags: [requires_find_command]
(function() {
    "use strict";

    load("jstests/libs/collection_drop_recreate.js");  // For assertDropAndRecreateCollection.
    load('jstests/replsets/libs/two_phase_drops.js');  // For TwoPhaseDropCollectionTest.

    // For supportsMajorityReadConcern.
    load('jstests/multiVersion/libs/causal_consistency_helpers.js');

    if (!supportsMajorityReadConcern()) {
        jsTestLog("Skipping test since storage engine doesn't support majority read concern.");
        return;
    }

    const st = new ShardingTest({
        shards: 2,
        rs: {
            nodes: 1,
            enableMajorityReadConcern: '',
        }
    });

    const mongersDB = st.s0.getDB(jsTestName());
    const mongersColl = mongersDB[jsTestName()];

    assert.commandWorked(mongersDB.dropDatabase());

    // Enable sharding on the test DB and ensure its primary is st.shard0.shardName.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.rs0.getURL());

    // Shard the test collection on a field called 'shardKey'.
    assert.commandWorked(
        mongersDB.adminCommand({shardCollection: mongersColl.getFullName(), key: {shardKey: 1}}));

    // Split the collection into 2 chunks: [MinKey, 0), [0, MaxKey].
    assert.commandWorked(
        mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {shardKey: 0}}));

    // Move the [0, MaxKey] chunk to st.shard1.shardName.
    assert.commandWorked(mongersDB.adminCommand(
        {moveChunk: mongersColl.getFullName(), find: {shardKey: 1}, to: st.rs1.getURL()}));

    // Write a document to each chunk.
    assert.writeOK(mongersColl.insert({shardKey: -1, _id: -1}, {writeConcern: {w: "majority"}}));
    assert.writeOK(mongersColl.insert({shardKey: 1, _id: 1}, {writeConcern: {w: "majority"}}));

    let changeStream = mongersColl.watch();

    // We awaited the replication of the first writes, so the change stream shouldn't return them.
    assert.writeOK(mongersColl.update({shardKey: -1, _id: -1}, {$set: {updated: true}}));
    assert.writeOK(mongersColl.update({shardKey: 1, _id: 1}, {$set: {updated: true}}));
    assert.writeOK(mongersColl.insert({shardKey: 2, _id: 2}));

    // Drop the collection and test that we return a "drop" entry, followed by an "invalidate"
    // entry.
    mongersColl.drop();

    // Test that we see the two writes that happened before the collection drop.
    assert.soon(() => changeStream.hasNext());
    let next = changeStream.next();
    assert.eq(next.operationType, "update");
    assert.eq(next.documentKey.shardKey, -1);
    const resumeTokenFromFirstUpdate = next._id;

    assert.soon(() => changeStream.hasNext());
    next = changeStream.next();
    assert.eq(next.operationType, "update");
    assert.eq(next.documentKey.shardKey, 1);

    assert.soon(() => changeStream.hasNext());
    next = changeStream.next();
    assert.eq(next.operationType, "insert");
    assert.eq(next.documentKey, {_id: 2});

    assert.soon(() => changeStream.hasNext());
    next = changeStream.next();
    assert.eq(next.operationType, "drop");
    assert.eq(next.ns, {db: mongersDB.getName(), coll: mongersColl.getName()});

    assert.soon(() => changeStream.hasNext());
    next = changeStream.next();
    assert.eq(next.operationType, "invalidate");
    assert(changeStream.isExhausted());

    // With an explicit collation, test that we can resume from before the collection drop.
    changeStream = mongersColl.watch(
        [], {resumeAfter: resumeTokenFromFirstUpdate, collation: {locale: "simple"}});

    assert.soon(() => changeStream.hasNext());
    next = changeStream.next();
    assert.eq(next.operationType, "update");
    assert.eq(next.documentKey, {shardKey: 1, _id: 1});

    assert.soon(() => changeStream.hasNext());
    next = changeStream.next();
    assert.eq(next.operationType, "insert");
    assert.eq(next.documentKey, {shardKey: 2, _id: 2});

    assert.soon(() => changeStream.hasNext());
    next = changeStream.next();
    assert.eq(next.operationType, "drop");
    assert.eq(next.ns, {db: mongersDB.getName(), coll: mongersColl.getName()});

    assert.soon(() => changeStream.hasNext());
    next = changeStream.next();
    assert.eq(next.operationType, "invalidate");
    assert(changeStream.isExhausted());

    // Test that we can resume the change stream without specifying an explicit collation.
    assert.commandWorked(mongersDB.runCommand({
        aggregate: mongersColl.getName(),
        pipeline: [{$changeStream: {resumeAfter: resumeTokenFromFirstUpdate}}],
        cursor: {}
    }));

    // Recreate and shard the collection.
    assert.commandWorked(mongersDB.createCollection(mongersColl.getName()));

    // Shard the test collection on shardKey.
    assert.commandWorked(
        mongersDB.adminCommand({shardCollection: mongersColl.getFullName(), key: {shardKey: 1}}));

    // Test that resuming the change stream on the recreated collection succeeds, since we will not
    // attempt to inherit the collection's default collation and can therefore ignore the new UUID.
    assert.commandWorked(mongersDB.runCommand({
        aggregate: mongersColl.getName(),
        pipeline: [{$changeStream: {resumeAfter: resumeTokenFromFirstUpdate}}],
        cursor: {}
    }));

    // Recreate the collection as unsharded and open a change stream on it.
    assertDropAndRecreateCollection(mongersDB, mongersColl.getName());

    changeStream = mongersColl.watch();

    // Drop the database and verify that the stream returns a collection drop followed by an
    // invalidate.
    assert.commandWorked(mongersDB.dropDatabase());

    assert.soon(() => changeStream.hasNext());
    next = changeStream.next();
    assert.eq(next.operationType, "drop");
    assert.eq(next.ns, {db: mongersDB.getName(), coll: mongersColl.getName()});

    assert.soon(() => changeStream.hasNext());
    next = changeStream.next();
    assert.eq(next.operationType, "invalidate");
    assert(changeStream.isExhausted());

    st.stop();
})();
