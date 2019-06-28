/**
 * Tests that the 'prepareTransaction' command fails against a standalone node.
 *
 * @tags: [uses_transactions, uses_prepare_transaction]
 */
(function() {
    "use strict";

    const standalone = MongerRunner.runMongerd();

    const collName = "prepare_transaction_fails_on_standalone";
    const dbName = "test";
    const testDB = standalone.getDB(dbName);

    assert.commandWorked(testDB.runCommand({create: collName}));

    assert.commandFailedWithCode(testDB.adminCommand({prepareTransaction: 1}), 51239);

    MongerRunner.stopMongerd(standalone);
}());
