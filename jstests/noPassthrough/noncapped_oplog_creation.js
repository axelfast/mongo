/**
 * Test that the server returns an error response for operations that attempt to create a non-capped
 * oplog collection.
 */
(function() {
    'use strict';

    var dbpath = MongerRunner.dataPath + 'noncapped_oplog_creation';
    resetDbpath(dbpath);

    var conn = MongerRunner.runMongerd({
        dbpath: dbpath,
        noCleanData: true,
    });
    assert.neq(null, conn, 'mongerd was unable to start up');

    var localDB = conn.getDB('local');

    // Test that explicitly creating a non-capped oplog collection fails.
    assert.commandFailed(localDB.createCollection('oplog.fake', {capped: false}));

    // Test that inserting into the replica set oplog fails when implicitly creating a non-capped
    // collection.
    assert.writeError(localDB.oplog.rs.insert({}));

    // Test that inserting into the master-slave oplog fails when implicitly creating a non-capped
    // collection.
    assert.commandFailed(localDB.runCommand({godinsert: 'oplog.$main', obj: {}}));

    // Test that creating a non-capped oplog collection fails when using $out.
    assert.writeOK(localDB.input.insert({}));
    assert.commandFailed(localDB.runCommand({
        aggregate: 'input',
        pipeline: [{$out: 'oplog.aggregation'}],
    }));

    MongerRunner.stopMongerd(conn);
})();
