/**
 * Test that a legacy query via mongers retains the $comment query meta-operator when transformed
 * into a find command for the shards. In addition, verify that the find command comment parameter
 * and query operator are passed to the shards correctly, and that an attempt to attach a non-string
 * comment to the find command fails.
 */
(function() {
    "use strict";

    // For profilerHasSingleMatchingEntryOrThrow.
    load("jstests/libs/profiler.js");

    const st = new ShardingTest({name: "mongers_comment_test", mongers: 1, shards: 1});

    const shard = st.shard0;
    const mongers = st.s;

    // Need references to the database via both mongers and mongerd so that we can enable profiling &
    // test queries on the shard.
    const mongersDB = mongers.getDB("mongers_comment");
    const shardDB = shard.getDB("mongers_comment");

    assert.commandWorked(mongersDB.dropDatabase());

    const mongersColl = mongersDB.test;
    const shardColl = shardDB.test;

    const collNS = mongersColl.getFullName();

    for (let i = 0; i < 5; ++i) {
        assert.writeOK(mongersColl.insert({_id: i, a: i}));
    }

    // The profiler will be used to verify that comments are present on the shard.
    assert.commandWorked(shardDB.setProfilingLevel(2));
    const profiler = shardDB.system.profile;

    //
    // Set legacy read mode for the mongers and shard connections.
    //
    mongersDB.getMongo().forceReadMode("legacy");
    shardDB.getMongo().forceReadMode("legacy");

    // TEST CASE: A legacy string $comment meta-operator is propagated to the shards via mongers.
    assert.eq(mongersColl.find({$query: {a: 1}, $comment: "TEST"}).itcount(), 1);
    profilerHasSingleMatchingEntryOrThrow(
        {profileDB: shardDB, filter: {op: "query", ns: collNS, "command.comment": "TEST"}});

    // TEST CASE: A legacy BSONObj $comment is converted to a string and propagated via mongers.
    assert.eq(mongersColl.find({$query: {a: 1}, $comment: {c: 2, d: {e: "TEST"}}}).itcount(), 1);
    profilerHasSingleMatchingEntryOrThrow({
        profileDB: shardDB,
        filter: {op: "query", ns: collNS, "command.comment": "{ c: 2.0, d: { e: \"TEST\" } }"}
    });

    // TEST CASE: Legacy BSONObj $comment is NOT converted to a string when issued on the mongerd.
    assert.eq(shardColl.find({$query: {a: 1}, $comment: {c: 3, d: {e: "TEST"}}}).itcount(), 1);
    profilerHasSingleMatchingEntryOrThrow({
        profileDB: shardDB,
        filter: {op: "query", ns: collNS, "command.comment": {c: 3, d: {e: "TEST"}}}
    });

    //
    // Revert to "commands" read mode for the find command test cases below.
    //
    mongersDB.getMongo().forceReadMode("commands");
    shardDB.getMongo().forceReadMode("commands");

    // TEST CASE: Verify that string find.comment and non-string find.filter.$comment propagate.
    assert.eq(mongersColl.find({a: 1, $comment: {b: "TEST"}}).comment("TEST").itcount(), 1);
    profilerHasSingleMatchingEntryOrThrow({
        profileDB: shardDB,
        filter: {
            op: "query",
            ns: collNS, "command.comment": "TEST", "command.filter.$comment": {b: "TEST"}
        }
    });

    // TEST CASE: Verify that find command with a non-string comment parameter is rejected.
    assert.commandFailedWithCode(
        mongersDB.runCommand(
            {"find": mongersColl.getName(), "filter": {a: 1}, "comment": {b: "TEST"}}),
        9,
        "Non-string find command comment did not return an error.");

    st.stop();
})();