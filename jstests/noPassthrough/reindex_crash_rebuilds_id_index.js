/**
 * If reIndex crashes after dropping indexes but before rebuilding them, a collection may exist
 * without an _id index. On startup, mongerd should automatically build any missing _id indexes.
 *
 * @tags: [
 *   requires_journaling,
 *   requires_persistence
 * ]
 */
(function() {

    load("jstests/libs/get_index_helpers.js");  // For GetIndexHelpers.

    const baseName = 'reindex_crash_rebuilds_id_index';
    const collName = baseName;
    const dbpath = MongerRunner.dataPath + baseName + '/';
    resetDbpath(dbpath);

    const mongerdOptions = {dbpath: dbpath, noCleanData: true};
    let conn = MongerRunner.runMongerd(mongerdOptions);

    let testDB = conn.getDB('test');
    let testColl = testDB.getCollection(collName);

    // Insert a single document and create the collection.
    testColl.insert({a: 1});
    let spec = GetIndexHelpers.findByKeyPattern(testColl.getIndexes(), {_id: 1});
    assert.neq(null, spec, "_id index not found");
    assert.eq("_id_", spec.name, tojson(spec));

    // Enable a failpoint that causes reIndex to crash after dropping the indexes but before
    // rebuilding them.
    assert.commandWorked(
        testDB.adminCommand({configureFailPoint: 'reIndexCrashAfterDrop', mode: 'alwaysOn'}));
    assert.throws(() => testColl.runCommand({reIndex: collName}));

    // The server should have crashed from the failpoint.
    MongerRunner.stopMongerd(conn, null, {allowedExitCode: MongerRunner.EXIT_ABRUPT});

    // The server should start up successfully after rebuilding the _id index.
    conn = MongerRunner.runMongerd(mongerdOptions);
    testDB = conn.getDB('test');
    testColl = testDB.getCollection(collName);
    assert(testColl.exists());

    // The _id index should exist.
    spec = GetIndexHelpers.findByKeyPattern(testColl.getIndexes(), {_id: 1});
    assert.neq(null, spec, "_id index not found");
    assert.eq("_id_", spec.name, tojson(spec));

    MongerRunner.stopMongerd(conn);
})();
