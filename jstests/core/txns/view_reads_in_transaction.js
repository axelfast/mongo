// Tests that reads on views are supported in transactions.
// @tags: [uses_transactions, uses_snapshot_read_concern]
(function() {
    "use strict";

    const session = db.getMonger().startSession({causalConsistency: false});
    const testDB = session.getDatabase("test");
    const coll = testDB.getCollection("view_reads_in_transaction_data_coll");
    const view = testDB.getCollection("view_reads_in_transaction_actual_view");

    coll.drop({writeConcern: {w: "majority"}});
    view.drop({writeConcern: {w: "majority"}});

    // Populate the backing collection.
    const testDoc = {_id: "kyle"};
    assert.commandWorked(coll.insert(testDoc, {writeConcern: {w: "majority"}}));

    // Create an identity view on the data-bearing collection.
    assert.commandWorked(view.runCommand(
        "create", {viewOn: coll.getName(), pipeline: [], writeConcern: {w: "majority"}}));

    const isMongers = assert.commandWorked(db.runCommand("ismaster")).msg === "isdbgrid";
    if (isMongers) {
        // Refresh the router's and shard's database versions so the distinct run below can succeed.
        // This is necessary because shards always abort their local transaction on stale version
        // errors and mongers is not allowed to retry on these errors in a transaction if the stale
        // shard has completed at least one earlier statement.
        assert.eq(view.distinct("_id"), ["kyle"]);
    }

    // Run a dummy find to start the transaction.
    jsTestLog("Starting transaction.");
    session.startTransaction({readConcern: {level: "snapshot"}});
    let cursor = coll.find();
    cursor.next();

    // Insert a document outside of the transaction. Subsequent reads should not see this document.
    jsTestLog("Inserting document outside of transaction.");
    assert.commandWorked(db.getSiblingDB(testDB.getName()).getCollection(coll.getName()).insert({
        _id: "not_visible_in_transaction",
    }));

    // Perform reads on views, which will be transformed into aggregations on the backing
    // collection.
    jsTestLog("Performing reads on the view inside the transaction.");
    cursor = view.find();
    assert.docEq(testDoc, cursor.next());
    assert(!cursor.hasNext());

    cursor = view.aggregate({$match: {}});
    assert.docEq(testDoc, cursor.next());
    assert(!cursor.hasNext());

    assert.eq(view.find({_id: {$exists: 1}}).itcount(), 1);

    assert.eq(view.distinct("_id"), ["kyle"]);

    assert.commandWorked(session.commitTransaction_forTesting());
    jsTestLog("Transaction committed.");
}());
