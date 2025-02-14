/**
 * Tests that a second duplicate createIndexes cmd request will wait for the first createIndexes cmd
 * request to finish before proceeding to either: return OK; or try to build the index again.
 *
 * Sets up paused index builds via failpoints and a parallel shell.
 *
 * First tests that the second request returns OK after finding the index ready after waiting;
 * then tests that the second request builds the index after waiting and finding the index does
 * not exist.
 *
 * @tags: [
 *     # Uses failpoints that the mongers does not have.
 *     assumes_against_mongerd_not_mongers,
 *     # Sets a failpoint on one mongerd, so switching primaries would break the test.
 *     does_not_support_stepdowns,
 *     # A write takes a global exclusive lock on the mobile engine, so two concurrent writers
 *     # (index builds) are impossible.
 *     # The ephemeralForTest engine has collection level locking, meaning that it upgrades
 *     # collection intent locks to exclusive. This test depends on two concurrent ops taking
 *     # concurrent collection IX locks.
 *     requires_document_locking,
 * ]
 */

(function() {
    "use strict";

    load("jstests/libs/check_log.js");
    load("jstests/libs/parallel_shell_helpers.js");
    load('jstests/libs/test_background_ops.js');

    const dbName = "test";
    const collName = "create_indexes_waits_for_already_in_progress";
    const testDB = db.getSiblingDB(dbName);
    const testColl = testDB.getCollection(collName);
    const indexSpecB = {key: {b: 1}, name: "the_b_1_index"};
    const indexSpecC = {key: {c: 1}, name: "the_c_1_index"};

    testColl.drop();
    assert.commandWorked(testDB.adminCommand({clearLog: 'global'}));

    // TODO (SERVER-40952): currently createIndexes will hold an X lock for the duration of the
    // build if the collection is not created beforehand. This test needs that not to happen, so we
    // can pause a build and a subsequently issued request can get an IX lock.
    assert.commandWorked(testDB.runCommand({create: collName}));

    function runSuccessfulIndexBuild(dbName, collName, indexSpec, requestNumber) {
        jsTest.log("Index build request " + requestNumber + " starting...");
        const res =
            db.getSiblingDB(dbName).runCommand({createIndexes: collName, indexes: [indexSpec]});
        jsTest.log("Index build request " + requestNumber + ", expected to succeed, result: " +
                   tojson(res));
        assert.commandWorked(res);
    }

    assert.commandWorked(testDB.adminCommand(
        {configureFailPoint: 'hangAfterSettingUpIndexBuild', mode: 'alwaysOn'}));
    let joinFirstIndexBuild;
    let joinSecondIndexBuild;
    try {
        jsTest.log("Starting a parallel shell to run first index build request...");
        joinFirstIndexBuild = startParallelShell(
            funWithArgs(runSuccessfulIndexBuild, dbName, collName, indexSpecB, 1),
            db.getMonger().port);

        jsTest.log("Waiting for first index build to get started...");
        checkLog.contains(db.getMonger(),
                          "Hanging index build due to failpoint 'hangAfterSettingUpIndexBuild'");

        jsTest.log("Starting a parallel shell to run second index build request...");
        joinSecondIndexBuild = startParallelShell(
            funWithArgs(runSuccessfulIndexBuild, dbName, collName, indexSpecB, 2),
            db.getMonger().port);

        jsTest.log("Waiting for second index build request to wait behind the first...");
        checkLog.contains(db.getMonger(),
                          "but found that at least one of the indexes is already being built");
    } finally {
        assert.commandWorked(
            testDB.adminCommand({configureFailPoint: 'hangAfterSettingUpIndexBuild', mode: 'off'}));
    }

    // The second request stalled behind the first, so now all we need to do is check that they both
    // complete successfully.
    joinFirstIndexBuild();
    joinSecondIndexBuild();

    // Make sure the parallel shells sucessfully built the index. We should have the _id index and
    // the 'the_b_1_index' index just built in the parallel shells.
    assert.eq(testColl.getIndexes().length, 2);

    // Lastly, if the first request fails transiently, then the second should restart the index
    // build.
    assert.commandWorked(testDB.adminCommand({clearLog: 'global'}));

    function runFailedIndexBuild(dbName, collName, indexSpec, requestNumber) {
        const res =
            db.getSiblingDB(dbName).runCommand({createIndexes: collName, indexes: [indexSpec]});
        jsTest.log("Index build request " + requestNumber + ", expected to fail, result: " +
                   tojson(res));
        assert.commandFailedWithCode(res, ErrorCodes.InternalError);
    }

    assert.commandWorked(
        testDB.adminCommand({configureFailPoint: 'hangAndThenFailIndexBuild', mode: 'alwaysOn'}));
    let joinFailedIndexBuild;
    let joinSuccessfulIndexBuild;
    try {
        jsTest.log("Starting a parallel shell to run third index build request...");
        joinFailedIndexBuild = startParallelShell(
            funWithArgs(runFailedIndexBuild, dbName, collName, indexSpecC, 3), db.getMonger().port);

        jsTest.log("Waiting for third index build to get started...");
        checkLog.contains(db.getMonger(),
                          "Hanging index build due to failpoint 'hangAndThenFailIndexBuild'");

        jsTest.log("Starting a parallel shell to run fourth index build request...");
        joinSuccessfulIndexBuild = startParallelShell(
            funWithArgs(runSuccessfulIndexBuild, dbName, collName, indexSpecC, 4),
            db.getMonger().port);

        jsTest.log("Waiting for fourth index build request to wait behind the third...");
        checkLog.contains(db.getMonger(),
                          "but found that at least one of the indexes is already being built");
    } finally {
        assert.commandWorked(
            testDB.adminCommand({configureFailPoint: 'hangAndThenFailIndexBuild', mode: 'off'}));
    }

    // The second request stalled behind the first, so now all we need to do is check that they both
    // complete as expected: the first should fail; the second should succeed.
    joinFailedIndexBuild();
    joinSuccessfulIndexBuild();

    // Make sure the parallel shells sucessfully built the index. We should now have the _id index,
    // the 'the_b_1_index' index and the 'the_c_1_index' just built in the parallel shells.
    assert.eq(testColl.getIndexes().length, 3);
})();
