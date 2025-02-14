/**
 * Tests that a change stream can be resumed from the higher of two tokens on separate shards whose
 * clusterTime is identical, differing only by documentKey, without causing the PBRT sent to mongerS
 * to go back-in-time.
 * @tags: [requires_replication, requires_journaling, requires_majority_read_concern]
 */
(function() {
    "use strict";

    const st =
        new ShardingTest({shards: 2, rs: {nodes: 1, setParameter: {writePeriodicNoops: false}}});

    const mongersDB = st.s.startSession({causalConsistency: true}).getDatabase(jsTestName());
    const mongersColl = mongersDB.test;

    // Enable sharding on the test DB and ensure its primary is shard0.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.rs0.getURL());

    // Shard on {_id:1}, split at {_id:0}, and move the upper chunk to shard1.
    st.shardColl(mongersColl, {_id: 1}, {_id: 0}, {_id: 1}, mongersDB.getName(), true);

    // Write one document to each shard.
    assert.commandWorked(mongersColl.insert({_id: -10}));
    assert.commandWorked(mongersColl.insert({_id: 10}));

    // Open a change stream cursor to listen for subsequent events.
    let csCursor = mongersColl.watch([], {cursor: {batchSize: 1}});

    // Update both documents in the collection, such that the events are likely to have the same
    // clusterTime. We update twice to ensure that the PBRT for both shards moves past the first two
    // updates.
    assert.commandWorked(mongersColl.update({}, {$set: {updated: 1}}, {multi: true}));
    assert.commandWorked(mongersColl.update({}, {$set: {updatedAgain: 1}}, {multi: true}));

    // Retrieve the first two events and confirm that they are in order with non-descending
    // clusterTime. Unfortunately we cannot guarantee that clusterTime will be identical, since it
    // is based on each shard's local value and there are operations beyond noop write that can
    // bump the oplog timestamp. We expect however that they will be identical for most test runs,
    // so there is value in testing.
    let clusterTime = null, updateEvent = null;
    for (let x = 0; x < 2; ++x) {
        assert.soon(() => csCursor.hasNext());
        updateEvent = csCursor.next();
        clusterTime = (clusterTime || updateEvent.clusterTime);
        assert.gte(updateEvent.clusterTime, clusterTime);
        assert.eq(updateEvent.updateDescription.updatedFields.updated, 1);
    }
    assert.soon(() => csCursor.hasNext());

    // Update both documents again, so that we will have something to observe after resuming.
    assert.commandWorked(mongersColl.update({}, {$set: {updatedYetAgain: 1}}, {multi: true}));

    // Resume from the second update, and confirm that we only see events starting with the third
    // and fourth updates. We use batchSize:1 to induce mongerD to send each individual event to the
    // mongerS when resuming, rather than scanning all the way to the most recent point in its oplog.
    csCursor = mongersColl.watch([], {resumeAfter: updateEvent._id, cursor: {batchSize: 1}});
    clusterTime = updateEvent = null;
    for (let x = 0; x < 2; ++x) {
        assert.soon(() => csCursor.hasNext());
        updateEvent = csCursor.next();
        clusterTime = (clusterTime || updateEvent.clusterTime);
        assert.gte(updateEvent.clusterTime, clusterTime);
        assert.eq(updateEvent.updateDescription.updatedFields.updatedAgain, 1);
    }
    assert.soon(() => csCursor.hasNext());

    st.stop();
})();
