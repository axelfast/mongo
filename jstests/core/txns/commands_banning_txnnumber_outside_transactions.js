// Test that commands other than retryable writes may not use txnNumber outside transactions.
// @tags: [
//   requires_document_locking,
// ]
(function() {
    "use strict";

    const isMongers = assert.commandWorked(db.runCommand("ismaster")).msg === "isdbgrid";

    const session = db.getMonger().startSession();
    const sessionDb = session.getDatabase("admin");

    const nonRetryableWriteCommands = [
        // Commands that are allowed in transactions.
        {aggregate: 1},
        {commitTransaction: 1},
        {distinct: "c"},
        {find: "c"},
        {getMore: NumberLong(1), collection: "c"},
        {killCursors: 1},
        // A selection of commands that are not allowed in transactions.
        {count: 1},
        {explain: {find: "c"}},
        {filemd5: 1},
        {isMaster: 1},
        {buildInfo: 1},
        {ping: 1},
        {listCommands: 1},
        {create: "c"},
        {drop: 1},
        {createIndexes: 1},
        {mapReduce: "c"}
    ];

    const nonRetryableWriteCommandsMongerdOnly = [
        // Commands that are allowed in transactions.
        {coordinateCommitTransaction: 1, participants: []},
        {geoSearch: 1},
        {prepareTransaction: 1},
        // A selection of commands that are not allowed in transactions.
        {applyOps: 1}
    ];

    nonRetryableWriteCommands.forEach(function(command) {
        jsTest.log("Testing command: " + tojson(command));
        assert.commandFailedWithCode(
            sessionDb.runCommand(Object.assign({}, command, {txnNumber: NumberLong(0)})),
            [50768, 50889]);
    });

    if (!isMongers) {
        nonRetryableWriteCommandsMongerdOnly.forEach(function(command) {
            jsTest.log("Testing command: " + tojson(command));
            assert.commandFailedWithCode(
                sessionDb.runCommand(Object.assign({}, command, {txnNumber: NumberLong(0)})),
                [50768, 50889]);
        });
    }

    session.endSession();
}());
