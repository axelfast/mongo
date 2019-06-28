/**
 * Tests that single shard transactions succeed against replica sets that contain arbiters.
 *
 * @tags: [uses_transactions, requires_find_command]
 */

(function() {
    "use strict";

    const name = "single_shard_transaction_with_arbiter";
    const dbName = "test";
    const collName = name;

    const shardingTest = new ShardingTest({
        shards: 1,
        rs: {
            nodes: [
                {/* primary */},
                {/* secondary */ rsConfig: {priority: 0}},
                {/* arbiter */ rsConfig: {arbiterOnly: true}}
            ]
        }
    });

    const mongers = shardingTest.s;
    const mongersDB = mongers.getDB(dbName);
    const mongersColl = mongersDB[collName];

    // Create and shard collection beforehand.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    assert.commandWorked(
        mongersDB.adminCommand({shardCollection: mongersColl.getFullName(), key: {_id: 1}}));
    assert.commandWorked(mongersColl.insert({_id: 1}, {writeConcern: {w: "majority"}}));

    const session = mongers.startSession();
    const sessionDB = session.getDatabase(dbName);
    const sessionColl = sessionDB.getCollection(collName);

    // Start a transaction and verify that it succeeds.
    session.startTransaction();
    assert.commandWorked(sessionColl.insert({_id: 0}));
    assert.commandWorked(session.commitTransaction_forTesting());

    assert.eq({_id: 0}, sessionColl.findOne({_id: 0}));

    shardingTest.stop();
})();
