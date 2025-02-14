// Tests that a mongers will correctly retry a stale shard version when read preference is used
(function() {
    'use strict';

    var st = new ShardingTest({
        shards: {rs0: {quiet: ''}, rs1: {quiet: ''}},
        mongers: 2,
        other: {mongersOptions: {verbose: 2}}
    });

    var testDB1 = st.s0.getDB('test');
    var testDB2 = st.s1.getDB('test');

    // Trigger a query on mongers 1 so it will have a view of test.user as being unsharded.
    testDB1.user.findOne();

    assert.commandWorked(testDB2.adminCommand({enableSharding: 'test'}));
    assert.commandWorked(testDB2.adminCommand({shardCollection: 'test.user', key: {x: 1}}));
    assert.commandWorked(testDB2.adminCommand({split: 'test.user', middle: {x: 100}}));

    var configDB2 = st.s1.getDB('config');
    var chunkToMove = configDB2.chunks.find().sort({min: 1}).next();
    var toShard = configDB2.shards.findOne({_id: {$ne: chunkToMove.shard}})._id;
    assert.commandWorked(
        testDB2.adminCommand({moveChunk: 'test.user', to: toShard, find: {x: 50}}));

    // Insert a document into each chunk
    assert.writeOK(testDB2.user.insert({x: 30}));
    assert.writeOK(testDB2.user.insert({x: 130}));

    // The testDB1 mongers does not know the chunk has been moved, and will retry
    var cursor = testDB1.user.find({x: 30}).readPref('primary');
    assert(cursor.hasNext());
    assert.eq(30, cursor.next().x);

    cursor = testDB1.user.find({x: 130}).readPref('primary');
    assert(cursor.hasNext());
    assert.eq(130, cursor.next().x);

    st.stop();
})();
