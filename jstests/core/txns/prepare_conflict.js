/**
 * Tests that prepare conflicts for prepared transactions are retried.
 *
 * @tags: [uses_transactions, uses_prepare_transaction]
 */
(function() {
    "use strict";
    load("jstests/core/txns/libs/prepare_helpers.js");

    const dbName = "test";
    const collName = "prepare_conflict";
    const testDB = db.getSiblingDB(dbName);
    const testColl = testDB.getCollection(collName);

    testColl.drop({writeConcern: {w: "majority"}});
    assert.commandWorked(testDB.runCommand({create: collName, writeConcern: {w: "majority"}}));

    function assertPrepareConflict(filter, clusterTime) {
        // Use a 5 second timeout so that there is enough time for the prepared transaction to
        // release its locks and for the command to obtain those locks.
        assert.commandFailedWithCode(
            // Use afterClusterTime read to make sure that it will block on a prepare conflict.
            testDB.runCommand({
                find: collName,
                filter: filter,
                readConcern: {afterClusterTime: clusterTime},
                maxTimeMS: 5000
            }),
            ErrorCodes.MaxTimeMSExpired);

        let prepareConflicted = false;
        const cur =
            testDB.system.profile.find({"ns": testColl.getFullName(), "command.filter": filter});
        while (cur.hasNext()) {
            const n = cur.next();
            print("op: " + JSON.stringify(n));
            if (n.prepareReadConflicts > 0) {
                prepareConflicted = true;
            }
        }
        assert(prepareConflicted);
    }

    // Insert a document modified by the transaction.
    const txnDoc = {_id: 1, x: 1};
    assert.commandWorked(testColl.insert(txnDoc));

    // Insert a document unmodified by the transaction.
    const otherDoc = {_id: 2, y: 2};
    assert.commandWorked(testColl.insert(otherDoc));

    // Create an index on 'y' to avoid conflicts on the field.
    assert.commandWorked(testColl.createIndex({y: 1}));

    // Enable the profiler to log slow queries. We expect a 'find' to hang until the prepare
    // conflict is resolved.
    assert.commandWorked(testDB.runCommand({profile: 1, level: 1, slowms: 100}));

    const session = db.getMonger().startSession({causalConsistency: false});
    const sessionDB = session.getDatabase(dbName);
    session.startTransaction({readConcern: {level: "snapshot"}});
    assert.commandWorked(sessionDB.runCommand({
        update: collName,
        updates: [{q: txnDoc, u: {$inc: {x: 1}}}],
    }));

    const prepareTimestamp = PrepareHelpers.prepareTransaction(session);

    // Conflict on _id of prepared document.
    assertPrepareConflict({_id: txnDoc._id}, prepareTimestamp);

    // Conflict on field that could be added to a prepared document.
    assertPrepareConflict({randomField: "random"}, prepareTimestamp);

    // No conflict on _id of a non-prepared document.
    assert.commandWorked(testDB.runCommand({find: collName, filter: {_id: otherDoc._id}}));

    // No conflict on indexed field of a non-prepared document.
    assert.commandWorked(testDB.runCommand({find: collName, filter: {y: otherDoc.y}}));

    // At this point, we can guarantee all subsequent reads will conflict. Do a read in a parallel
    // shell, abort the transaction, then ensure the read succeeded with the old document.
    TestData.collName = collName;
    TestData.dbName = dbName;
    TestData.txnDoc = txnDoc;
    const findAwait = startParallelShell(function() {
        const it = db.getSiblingDB(TestData.dbName)
                       .runCommand({find: TestData.collName, filter: {_id: TestData.txnDoc._id}});
    }, db.getMonger().port);

    assert.commandWorked(session.abortTransaction_forTesting());

    // The find command should be successful.
    findAwait({checkExitSuccess: true});

    // The document should be unmodified, because we aborted.
    assert.eq(txnDoc, testColl.findOne(txnDoc));
})();
