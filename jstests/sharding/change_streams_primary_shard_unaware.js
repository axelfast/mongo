// Tests the behavior of a change stream on a collection that becomes sharded, however the primary
// shard is unaware and still sees the collection as unsharded.
//
// This test triggers a compiler bug that causes a crash when compiling with optimizations on, see
// SERVER-36321.
// @tags: [requires_persistence, blacklist_from_rhel_67_s390x, uses_change_streams]
(function() {
    "use strict";

    load('jstests/libs/change_stream_util.js');  // For ChangeStreamTest.

    // For supportsMajorityReadConcern().
    load("jstests/multiVersion/libs/causal_consistency_helpers.js");

    // TODO (SERVER-38673): Remove this once BACKPORT-3428, BACKPORT-3429 are completed.
    if (!jsTestOptions().enableMajorityReadConcern &&
        jsTestOptions().mongersBinVersion === 'last-stable') {
        jsTestLog(
            "Skipping test since 'last-stable' mongers doesn't support speculative majority update lookup queries.");
        return;
    }

    if (!supportsMajorityReadConcern()) {
        jsTestLog("Skipping test since storage engine doesn't support majority read concern.");
        return;
    }

    // Returns true if the shard is aware that the collection is sharded.
    function isShardAware(shard, coll) {
        const res = shard.adminCommand({getShardVersion: coll, fullMetadata: true});
        assert.commandWorked(res);
        return res.metadata.collVersion != undefined;
    }

    const testName = "change_streams_primary_shard_unaware";
    const st = new ShardingTest({
        shards: 2,
        mongers: 3,
        rs: {
            nodes: 1,
            // Use a higher frequency for periodic noops to speed up the test.
            setParameter: {periodicNoopIntervalSecs: 1, writePeriodicNoops: true},
        },
    });

    const mongersDB = st.s0.getDB(testName);

    // Ensure that shard0 is the primary shard.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.rs0.getURL());

    // Create unsharded collection on primary shard.
    const mongersColl = mongersDB[testName];
    assert.commandWorked(mongersDB.createCollection(testName));

    // Before sharding the collection, issue a write through mongers2 to ensure that it knows the
    // collection exists and believes it is unsharded. This is needed later in the test to avoid
    // triggering a refresh when a change stream is established through mongers2.
    const mongers2DB = st.s2.getDB(testName);
    const mongers2Coll = mongers2DB[testName];
    assert.writeOK(mongers2Coll.insert({_id: 0, a: 0}));

    // Create index on the shard key.
    assert.commandWorked(mongers2Coll.createIndex({a: 1}));

    // Shard the collection.
    assert.commandWorked(
        mongersDB.adminCommand({shardCollection: mongersColl.getFullName(), key: {a: 1}}));

    // Restart the primary shard and ensure that it is no longer aware that the collection is
    // sharded.
    st.restartShardRS(0);
    assert.eq(false, isShardAware(st.rs0.getPrimary(), mongersColl.getFullName()));

    const mongers1DB = st.s1.getDB(testName);
    const mongers1Coll = mongers1DB[testName];

    // Establish change stream cursor on the second mongers, which is not aware that the
    // collection is sharded.
    let cstMongers1 = new ChangeStreamTest(mongers1DB);
    let cursorMongers1 = cstMongers1.startWatchingChanges(
        {pipeline: [{$changeStream: {fullDocument: "updateLookup"}}], collection: mongers1Coll});
    assert.eq(0, cursorMongers1.firstBatch.length, "Cursor had changes: " + tojson(cursorMongers1));

    // Establish a change stream cursor on the now sharded collection through the first mongers.
    let cst = new ChangeStreamTest(mongersDB);
    let cursor = cst.startWatchingChanges(
        {pipeline: [{$changeStream: {fullDocument: "updateLookup"}}], collection: mongersColl});
    assert.eq(0, cursor.firstBatch.length, "Cursor had changes: " + tojson(cursor));

    // Ensure that the primary shard is still unaware that the collection is sharded.
    assert.eq(false, isShardAware(st.rs0.getPrimary(), mongersColl.getFullName()));

    // Insert a doc and verify that the primary shard is now aware that the collection is sharded.
    assert.writeOK(mongersColl.insert({_id: 1, a: 1}));
    assert.eq(true, isShardAware(st.rs0.getPrimary(), mongersColl.getFullName()));

    // Verify that both cursors are able to pick up an inserted document.
    cst.assertNextChangesEqual({
        cursor: cursor,
        expectedChanges: [{
            documentKey: {_id: 1, a: 1},
            fullDocument: {_id: 1, a: 1},
            ns: {db: mongersDB.getName(), coll: mongersColl.getName()},
            operationType: "insert",
        }]
    });
    let mongers1ChangeDoc = cstMongers1.getOneChange(cursorMongers1);
    assert.docEq({_id: 1, a: 1}, mongers1ChangeDoc.documentKey);
    assert.docEq({_id: 1, a: 1}, mongers1ChangeDoc.fullDocument);
    assert.eq({db: mongers1DB.getName(), coll: mongers1Coll.getName()}, mongers1ChangeDoc.ns);
    assert.eq("insert", mongers1ChangeDoc.operationType);

    // Split the collection into 2 chunks: [MinKey, 0), [0, MaxKey).
    assert.commandWorked(mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {a: 0}}));

    // Move a chunk to the non-primary shard.
    assert.commandWorked(mongersDB.adminCommand({
        moveChunk: mongersColl.getFullName(),
        find: {a: -1},
        to: st.rs1.getURL(),
        _waitForDelete: true
    }));

    // Update the document on the primary shard.
    assert.writeOK(mongersColl.update({_id: 1, a: 1}, {$set: {b: 1}}));
    // Insert another document to each shard.
    assert.writeOK(mongersColl.insert({_id: -2, a: -2}));
    assert.writeOK(mongersColl.insert({_id: 2, a: 2}));

    // Verify that both cursors pick up the first inserted doc regardless of the moveChunk
    // operation.
    cst.assertNextChangesEqual({
        cursor: cursor,
        expectedChanges: [{
            documentKey: {_id: 1, a: 1},
            fullDocument: {_id: 1, a: 1, b: 1},
            ns: {db: mongersDB.getName(), coll: mongersColl.getName()},
            operationType: "update",
            updateDescription: {removedFields: [], updatedFields: {b: 1}}
        }]
    });
    mongers1ChangeDoc = cstMongers1.getOneChange(cursorMongers1);
    assert.docEq({_id: 1, a: 1}, mongers1ChangeDoc.documentKey);
    assert.docEq({_id: 1, a: 1, b: 1}, mongers1ChangeDoc.fullDocument);
    assert.eq({db: mongers1DB.getName(), coll: mongers1Coll.getName()}, mongers1ChangeDoc.ns);
    assert.eq("update", mongers1ChangeDoc.operationType);

    // Restart the primary shard and ensure that it is no longer aware that the collection is
    // sharded.
    st.restartShardRS(0);
    assert.eq(false, isShardAware(st.rs0.getPrimary(), mongersColl.getFullName()));

    // Establish change stream cursor on mongers2 using the resume token from the change steam on
    // mongers1. Mongers2 is aware that the collection exists and thinks that it's unsharded, so it
    // won't trigger a routing table refresh. This must be done using a resume token from an update
    // otherwise the shard will generate the documentKey based on the assumption that the shard key
    // is _id which will cause the cursor establishment to fail due to SERVER-32085.
    let cstMongers2 = new ChangeStreamTest(mongers2DB);
    let cursorMongers2 = cstMongers2.startWatchingChanges({
        pipeline: [{$changeStream: {resumeAfter: mongers1ChangeDoc._id}}],
        collection: mongers2Coll
    });

    cstMongers2.assertNextChangesEqual({
        cursor: cursorMongers2,
        expectedChanges: [{
            documentKey: {_id: -2, a: -2},
            fullDocument: {_id: -2, a: -2},
            ns: {db: mongers2DB.getName(), coll: mongers2Coll.getName()},
            operationType: "insert",
        }]
    });

    cstMongers2.assertNextChangesEqual({
        cursor: cursorMongers2,
        expectedChanges: [{
            documentKey: {_id: 2, a: 2},
            fullDocument: {_id: 2, a: 2},
            ns: {db: mongers2DB.getName(), coll: mongers2Coll.getName()},
            operationType: "insert",
        }]
    });

    st.stop();

})();
