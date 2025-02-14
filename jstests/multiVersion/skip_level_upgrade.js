/**
 * Tests that skip level upgrades from old binary versions to the latest binary version are both
 * not successful and do not corrupt anything to prevent correct binary version start up.
 *
 * For each binary version older than last-stable:
 * - Start a clean node of that version
 * - Create a new collection.
 * - Insert a document into the new collection.
 * - Create an index on the new collection.
 * - Benignly fail to upgrade to the latest version.
 * - Benignly fail to run --repair on the latest version.
 * - Successfully restart the node in the original version.
 * - Verify data files are intact.
 */

(function() {
    'use strict';

    load('jstests/libs/get_index_helpers.js');

    const dbpath = MongerRunner.dataPath + 'skip_level_upgrade';
    resetDbpath(dbpath);

    // We set noCleanData to true in order to preserve the data files within an iteration.
    const defaultOptions = {
        dbpath: dbpath,
        noCleanData: true,
    };

    // This lists all binary versions older than the last-stable version.
    // TODO SERVER-26792: In the future, we should have a common place from which both the
    // multiversion setup procedure and this test get information about supported major releases.
    const versions = [
        {binVersion: '3.2', testCollection: 'three_two'},
        {binVersion: '3.4', testCollection: 'three_four'},
        {binVersion: '3.6', testCollection: 'three_six'}
    ];

    // Iterate through versions specified in the versions list, and follow the steps outlined at
    // the top of this test file.
    for (let i = 0; i < versions.length; i++) {
        let version = versions[i];
        let mongerdOptions = Object.extend({binVersion: version.binVersion}, defaultOptions);

        // Start up an old binary version mongerd.
        let conn = MongerRunner.runMongerd(mongerdOptions);
        let port = conn.port;

        assert.neq(null,
                   conn,
                   'mongerd was unable able to start with version ' + tojson(version.binVersion));

        // Set up a collection on an old binary version node with one document and an index, and
        // then shut it down.
        let testDB = conn.getDB('test');
        assert.commandWorked(testDB.createCollection(version.testCollection));
        assert.writeOK(testDB[version.testCollection].insert({a: 1}));
        assert.commandWorked(testDB[version.testCollection].createIndex({a: 1}));
        MongerRunner.stopMongerd(conn);

        // Restart the mongerd with the latest binary version on the old version's data files.
        // Should fail due to being a skip level upgrade.
        mongerdOptions = Object.extend({binVersion: 'latest'}, defaultOptions);
        conn = MongerRunner.runMongerd(mongerdOptions);
        assert.eq(null, conn);

        // Restart the mongerd with the latest version with --repair. Should fail due to being a
        // skip level upgrade.
        let returnCode = runMongerProgram("mongerd", "--port", port, "--repair", "--dbpath", dbpath);
        assert.neq(returnCode, 0, "expected mongerd --repair to fail with a skip level upgrade");

        // Restart the mongerd in the originally specified version. Should succeed.
        mongerdOptions = Object.extend({binVersion: version.binVersion}, defaultOptions);
        conn = MongerRunner.runMongerd(mongerdOptions);

        // Verify that the data and indices from previous iterations are still accessible.
        testDB = conn.getDB('test');
        assert.eq(1,
                  testDB[version.testCollection].count(),
                  `data from ${version.testCollection} should be available; options: ` +
                      tojson(mongerdOptions));
        assert.neq(
            null,
            GetIndexHelpers.findByKeyPattern(testDB[version.testCollection].getIndexes(), {a: 1}),
            `index from ${version.testCollection} should be available; options: ` +
                tojson(mongerdOptions));

        MongerRunner.stopMongerd(conn);

        resetDbpath(dbpath);
    }
})();
