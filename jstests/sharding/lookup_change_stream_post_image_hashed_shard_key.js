// Tests the behavior of looking up the post image for change streams on collections which are
// sharded with a hashed shard key.
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
        enableBalancer: false,
        rs: {
            nodes: 1,
            enableMajorityReadConcern: '',
            // Use a higher frequency for periodic noops to speed up the test.
            setParameter: {writePeriodicNoops: true, periodicNoopIntervalSecs: 1}
        }
    });

    const mongersDB = st.s0.getDB(jsTestName());
    const mongersColl = mongersDB['coll'];

    assert.commandWorked(mongersDB.dropDatabase());

    // Enable sharding on the test DB and ensure its primary is st.shard0.shardName.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.rs0.getURL());

    // Shard the test collection on the field "shardKey", and split it into two chunks.
    assert.commandWorked(mongersDB.adminCommand({
        shardCollection: mongersColl.getFullName(),
        numInitialChunks: 2,
        key: {shardKey: "hashed"}
    }));

    // Make sure the negative chunk is on shard 0.
    assert.commandWorked(mongersDB.adminCommand({
        moveChunk: mongersColl.getFullName(),
        bounds: [{shardKey: MinKey}, {shardKey: NumberLong("0")}],
        to: st.rs0.getURL()
    }));

    // Make sure the positive chunk is on shard 1.
    assert.commandWorked(mongersDB.adminCommand({
        moveChunk: mongersColl.getFullName(),
        bounds: [{shardKey: NumberLong("0")}, {shardKey: MaxKey}],
        to: st.rs1.getURL()
    }));

    const changeStream = mongersColl.aggregate([{$changeStream: {fullDocument: "updateLookup"}}]);

    // Write enough documents that we likely have some on each shard.
    const nDocs = 1000;
    for (let id = 0; id < nDocs; ++id) {
        assert.writeOK(mongersColl.insert({_id: id, shardKey: id}));
        assert.writeOK(mongersColl.update({shardKey: id}, {$set: {updatedCount: 1}}));
    }

    for (let id = 0; id < nDocs; ++id) {
        assert.soon(() => changeStream.hasNext());
        let next = changeStream.next();
        assert.eq(next.operationType, "insert");
        assert.eq(next.documentKey, {shardKey: id, _id: id});

        assert.soon(() => changeStream.hasNext());
        next = changeStream.next();
        assert.eq(next.operationType, "update");
        assert.eq(next.documentKey, {shardKey: id, _id: id});
        assert.docEq(next.fullDocument, {_id: id, shardKey: id, updatedCount: 1});
    }

    st.stop();
})();
