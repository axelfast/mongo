/**
 * SERVER-20617: Tests that journaled write operations survive a kill -9 of the mongerd.
 *
 * This test requires persistence to ensure data survives a restart.
 * @tags: [requires_persistence]
 */
(function() {
    'use strict';

    //  The following test verifies that writeConcern: {j: true} ensures that data is durable.
    var dbpath = MongerRunner.dataPath + 'sync_write';
    resetDbpath(dbpath);

    var mongerdArgs = {dbpath: dbpath, noCleanData: true, journal: ''};

    // Start a mongerd.
    var conn = MongerRunner.runMongerd(mongerdArgs);
    assert.neq(null, conn, 'mongerd was unable to start up');

    // Now connect to the mongerd, do a journaled write and abruptly stop the server.
    var testDB = conn.getDB('test');
    assert.writeOK(testDB.synced.insert({synced: true}, {writeConcern: {j: true}}));
    MongerRunner.stopMongerd(conn, 9, {allowedExitCode: MongerRunner.EXIT_SIGKILL});

    // Restart the mongerd.
    conn = MongerRunner.runMongerd(mongerdArgs);
    assert.neq(null, conn, 'mongerd was unable to restart after receiving a SIGKILL');

    // Check that our journaled write still is present.
    testDB = conn.getDB('test');
    assert.eq(1, testDB.synced.count({synced: true}), 'synced write was not found');
    MongerRunner.stopMongerd(conn);
})();
