// Tests that commands that should not be runnable on sharded collections cannot be run on sharded
// collections.
(function() {

    const st = new ShardingTest({shards: 2, mongers: 2});

    const dbName = 'test';
    const coll = 'foo';
    const ns = dbName + '.' + coll;

    const freshMongers = st.s0.getDB(dbName);
    const staleMongers = st.s1.getDB(dbName);

    assert.commandWorked(staleMongers.adminCommand({enableSharding: dbName}));
    assert.commandWorked(freshMongers.adminCommand({shardCollection: ns, key: {_id: 1}}));

    // Test that commands that should not be runnable on sharded collection do not work on sharded
    // collections, using both fresh mongers and stale mongers instances.
    assert.commandFailedWithCode(freshMongers.runCommand({convertToCapped: coll, size: 64 * 1024}),
                                 ErrorCodes.IllegalOperation);
    assert.commandFailedWithCode(staleMongers.runCommand({convertToCapped: coll, size: 32 * 1024}),
                                 ErrorCodes.IllegalOperation);

    st.stop();

})();
