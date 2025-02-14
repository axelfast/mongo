/**
 * Tests that the listIndexes command shows ready and in-progress indexes.
 */
(function() {
    "use strict";

    load("jstests/noPassthrough/libs/index_build.js");

    const conn = MongerRunner.runMongerd();
    assert.neq(null, conn, "mongerd was unable to start up");

    const testDB = conn.getDB("test");
    assert.commandWorked(testDB.dropDatabase());

    let coll = testDB.list_indexes_ready_and_in_progress;
    coll.drop();
    assert.commandWorked(testDB.createCollection(coll.getName()));
    IndexBuildTest.assertIndexes(coll, 1, ["_id_"]);
    assert.commandWorked(coll.createIndex({a: 1}));
    IndexBuildTest.assertIndexes(coll, 2, ["_id_", "a_1"]);

    IndexBuildTest.pauseIndexBuilds(conn);
    const createIdx =
        IndexBuildTest.startIndexBuild(conn, coll.getFullName(), {b: 1}, {background: true});
    IndexBuildTest.waitForIndexBuildToStart(testDB);

    // The listIndexes command supports returning all indexes, including ones that are not ready.
    IndexBuildTest.assertIndexes(coll, 3, ["_id_", "a_1"], ["b_1"], {includeBuildUUIDs: true});

    IndexBuildTest.resumeIndexBuilds(conn);

    // Wait for the index build to stop.
    IndexBuildTest.waitForIndexBuildToStop(testDB);

    const exitCode = createIdx();
    assert.eq(0, exitCode, 'expected shell to exit cleanly');

    IndexBuildTest.assertIndexes(coll, 3, ["_id_", "a_1", "b_1"]);
    MongerRunner.stopMongerd(conn);
}());
