// @tags: [requires_non_retryable_writes, requires_fastcount]

// Tests for explaining the delete command.
(function() {
    "use strict";

    var collName = "jstests_explain_delete";
    var t = db[collName];
    t.drop();

    var explain;

    /**
     * Verify that the explain command output 'explain' shows a DELETE stage with an nWouldDelete
     * value equal to 'nWouldDelete'.
     */
    function checkNWouldDelete(explain, nWouldDelete) {
        assert.commandWorked(explain);
        assert("executionStats" in explain);
        var executionStats = explain.executionStats;
        assert("executionStages" in executionStats);

        // If passed through mongers, then DELETE stage(s) should be below the SHARD_WRITE mongers
        // stage.  Otherwise the DELETE stage is the root stage.
        var execStages = executionStats.executionStages;
        if ("SHARD_WRITE" === execStages.stage) {
            let totalToBeDeletedAcrossAllShards = 0;
            execStages.shards.forEach(function(shardExplain) {
                const rootStageName = shardExplain.executionStages.stage;
                assert.eq(rootStageName, "DELETE", tojson(execStages));
                totalToBeDeletedAcrossAllShards += shardExplain.executionStages.nWouldDelete;
            });
            assert.eq(totalToBeDeletedAcrossAllShards, nWouldDelete, explain);
        } else {
            assert.eq(execStages.stage, "DELETE", explain);
            assert.eq(execStages.nWouldDelete, nWouldDelete, explain);
        }
    }

    // Explain delete against an empty collection.
    assert.commandWorked(db.createCollection(t.getName()));
    explain = db.runCommand({explain: {delete: collName, deletes: [{q: {a: 1}, limit: 0}]}});
    checkNWouldDelete(explain, 0);

    // Add an index but no data, and check that the explain still works.
    t.ensureIndex({a: 1});
    explain = db.runCommand({explain: {delete: collName, deletes: [{q: {a: 1}, limit: 0}]}});
    checkNWouldDelete(explain, 0);

    // Add some copies of the same document.
    for (var i = 0; i < 10; i++) {
        t.insert({a: 1});
    }
    assert.eq(10, t.count());

    // Run an explain which shows that all 10 documents *would* be deleted.
    explain = db.runCommand({explain: {delete: collName, deletes: [{q: {a: 1}, limit: 0}]}});
    checkNWouldDelete(explain, 10);

    // Make sure all 10 documents are still there.
    assert.eq(10, t.count());

    // If we run the same thing without the explain, then all 10 docs should be deleted.
    var deleteResult = db.runCommand({delete: collName, deletes: [{q: {a: 1}, limit: 0}]});
    assert.commandWorked(deleteResult);
    assert.eq(0, t.count());
}());
