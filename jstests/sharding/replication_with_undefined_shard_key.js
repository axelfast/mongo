// Test for SERVER-31953 where secondaries crash when replicating an oplog entry where the document
// identifier in the oplog entry contains a shard key value that contains an undefined value.
(function() {
    "use strict";

    const st = new ShardingTest({mongers: 1, config: 1, shard: 1, rs: {nodes: 2}});
    const mongersDB = st.s.getDB("test");
    const mongersColl = mongersDB.mycoll;

    // Shard the test collection on the "x" field.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    assert.commandWorked(mongersDB.adminCommand({
        shardCollection: mongersColl.getFullName(),
        key: {x: 1},
    }));

    // Insert a document with a literal undefined value.
    assert.writeOK(mongersColl.insert({x: undefined}));

    jsTestLog("Doing writes that generate oplog entries including undefined document key");

    assert.writeOK(mongersColl.update(
        {},
        {$set: {a: 1}},
        {multi: true, writeConcern: {w: 2, wtimeout: ReplSetTest.kDefaultTimeoutMs}}));
    assert.writeOK(
        mongersColl.remove({}, {writeConcern: {w: 2, wtimeout: ReplSetTest.kDefaultTimeoutMs}}));

    st.stop();
})();