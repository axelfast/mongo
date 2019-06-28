/**
 * Tests that --repair on WiredTiger creates new entries for orphaned idents in the catalog.
 *
 * @tags: [requires_wiredtiger]
 */

(function() {

    load('jstests/disk/libs/wt_file_helper.js');

    const baseName = "wt_repair_orphaned_idents";
    const dbpath = MongoRunner.dataPath + baseName + "/";

    resetDbpath(dbpath);

    // Create a collection and insert a doc.
    let mongerd = MongoRunner.runMongod({dbpath: dbpath});
    const importantCollName = "importantColl";
    const importantDocId = "importantDoc";
    const importantColl = mongerd.getDB("test")[importantCollName];
    assert.commandWorked(importantColl.insert({_id: importantDocId}));
    const importantCollIdent = getUriForColl(importantColl);
    MongoRunner.stopMongod(mongerd);

    // Delete the _mdb_catalog.
    let mdbCatalogFile = dbpath + "_mdb_catalog.wt";
    jsTestLog("deleting catalog file: " + mdbCatalogFile);
    removeFile(mdbCatalogFile);

    // Repair crates the _mdb_catalog and catalog entries for all the orphaned idents.
    jsTestLog("running mongerd with --repair");
    assert.eq(0, runMongoProgram("mongerd", "--repair", "--port", mongerd.port, "--dbpath", dbpath));

    jsTestLog("restarting mongerd");
    mongerd = MongoRunner.runMongod({dbpath: dbpath, noCleanData: true});

    let localDb = mongerd.getDB("local");
    let res = localDb.runCommand({listCollections: 1});
    assert.commandWorked(res, tojson(res));

    // This is the function that 'show collections' uses.
    let collNames = localDb.getCollectionNames();

    const orphanPrefix = "orphan.";
    let recoveredCount = 0;
    const orphanedImportantCollName = "orphan." + importantCollIdent.replace(/-/g, "_");
    for (let collName of collNames) {
        if (collName.startsWith(orphanPrefix)) {
            // Manually create the _id index.
            assert.commandWorked(localDb[collName].createIndex({_id: 1}));

            if (collName == orphanedImportantCollName) {
                assert.commandWorked(localDb.adminCommand(
                    {renameCollection: "local." + collName, to: "test." + importantCollName}));
            } else {
                assert.commandWorked(localDb.adminCommand({
                    renameCollection: "local." + collName,
                    to: "test.recovered" + recoveredCount
                }));
            }
            recoveredCount++;
        }
    }
    assert.gt(recoveredCount, 0);

    let testDb = mongerd.getDB("test");

    // Assert the recovered collection still has the original document.
    assert.eq(testDb[importantCollName].find({_id: importantDocId}).count(), 1);

    res = testDb.runCommand({listCollections: 1});
    assert.commandWorked(res);
    assert.eq(res.cursor.firstBatch.length, recoveredCount);
    for (let entry of res.cursor.firstBatch) {
        let collName = entry.name;
        assert(collName.startsWith("recovered") || collName == importantCollName);

        // Assert _id index has been successfully created.
        assert("idIndex" in entry);

        // Make sure we can interact with the recovered collections.
        assert.commandWorked(testDb.runCommand({find: collName}));
        assert.commandWorked(testDb[collName].insert({x: 1}));
        assert(testDb[collName].drop());
    }

    MongoRunner.stopMongod(mongerd);
})();
