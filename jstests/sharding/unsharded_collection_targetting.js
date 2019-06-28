// Tests that a stale mongers would route writes correctly to the right shard after
// an unsharded collection was moved to another shard.
(function() {
    "use strict";

    const st = new ShardingTest({
        shards: 2,
        mongers: 2,
        rs: {
            nodes: 1,
        },
    });

    const testName = 'test';
    const mongersDB = st.s0.getDB(testName);

    // Ensure that shard1 is the primary shard.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.rs1.getURL());

    // Before moving the collection, issue a write through mongers2 to make it aware
    // about the location of the collection before the move.
    const mongers2DB = st.s1.getDB(testName);
    const mongers2Coll = mongers2DB[testName];
    assert.writeOK(mongers2Coll.insert({_id: 0, a: 0}));

    st.ensurePrimaryShard(mongersDB.getName(), st.rs0.getURL());

    assert.writeOK(mongers2Coll.insert({_id: 1, a: 0}));

    st.stop();
})();
