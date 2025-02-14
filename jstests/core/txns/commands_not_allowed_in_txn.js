// Test commands that are not allowed in multi-document transactions.
// @tags: [
//   uses_snapshot_read_concern,
//   uses_transactions,
// ]
(function() {
    "use strict";

    const dbName = "test";
    const collName = "commands_not_allowed_in_txn";
    const testDB = db.getSiblingDB(dbName);
    const testColl = testDB[collName];

    testDB.runCommand({drop: collName, writeConcern: {w: "majority"}});
    let txnNumber = 0;

    const sessionOptions = {causalConsistency: false};
    const session = db.getMonger().startSession(sessionOptions);
    const sessionDb = session.getDatabase(dbName);

    const isMongers = assert.commandWorked(db.runCommand("ismaster")).msg === "isdbgrid";

    assert.commandWorked(
        testDB.createCollection(testColl.getName(), {writeConcern: {w: "majority"}}));
    assert.commandWorked(testDB.runCommand({
        createIndexes: collName,
        indexes: [
            {name: "geo_2d", key: {geo: "2d"}},
            {key: {haystack: "geoHaystack", a: 1}, name: "haystack_geo", bucketSize: 1}
        ],
        writeConcern: {w: "majority"}
    }));

    function setup() {
        testColl.dropIndex({a: 1});
        testDB.runCommand({drop: "create_collection", writeConcern: {w: "majority"}});
        testDB.runCommand({drop: "drop_collection", writeConcern: {w: "majority"}});
        assert.commandWorked(
            testDB.createCollection("drop_collection", {writeConcern: {w: "majority"}}));
    }

    function testCommand(command) {
        jsTest.log("Testing command: " + tojson(command));
        const errmsgRegExp = new RegExp(
            'Cannot run .* in a multi-document transaction.\|This command is not supported in transactions');

        // Check that the command runs successfully outside transactions.
        setup();
        assert.commandWorked(sessionDb.runCommand(command));

        // Check that the command cannot be used to start a transaction.
        setup();
        let res = assert.commandFailedWithCode(sessionDb.runCommand(Object.assign({}, command, {
            readConcern: {level: "snapshot"},
            txnNumber: NumberLong(++txnNumber),
            stmtId: NumberInt(0),
            startTransaction: true,
            autocommit: false
        })),
                                               ErrorCodes.OperationNotSupportedInTransaction);
        // Check that the command fails with expected error message.
        assert(res.errmsg.match(errmsgRegExp), res);

        // Mongers has special handling for commitTransaction to support commit recovery.
        if (!isMongers) {
            assert.commandFailedWithCode(sessionDb.adminCommand({
                commitTransaction: 1,
                txnNumber: NumberLong(txnNumber),
                stmtId: NumberInt(1),
                autocommit: false
            }),
                                         ErrorCodes.NoSuchTransaction);
        }

        // Check that the command fails inside a transaction, but does not abort the transaction.
        setup();
        assert.commandWorked(sessionDb.runCommand({
            insert: collName,
            documents: [{}],
            readConcern: {level: "snapshot"},
            txnNumber: NumberLong(++txnNumber),
            stmtId: NumberInt(0),
            startTransaction: true,
            autocommit: false
        }));
        res = assert.commandFailedWithCode(
            sessionDb.runCommand(Object.assign(
                {},
                command,
                {txnNumber: NumberLong(txnNumber), stmtId: NumberInt(1), autocommit: false})),
            ErrorCodes.OperationNotSupportedInTransaction);
        // Check that the command fails with expected error message.
        assert(res.errmsg.match(errmsgRegExp), res);
        assert.commandWorked(sessionDb.adminCommand({
            commitTransaction: 1,
            txnNumber: NumberLong(txnNumber),
            stmtId: NumberInt(2),
            autocommit: false
        }));
    }

    //
    // Test a selection of commands that are not allowed in transactions.
    //

    const commands = [
        {count: collName},
        {count: collName, query: {a: 1}},
        {explain: {find: collName}},
        {filemd5: 1, root: "fs"},
        {isMaster: 1},
        {buildInfo: 1},
        {ping: 1},
        {listCommands: 1},
        {create: "create_collection", writeConcern: {w: "majority"}},
        {drop: "drop_collection", writeConcern: {w: "majority"}},
        {
          createIndexes: collName,
          indexes: [{name: "a_1", key: {a: 1}}],
          writeConcern: {w: "majority"}
        },
        // Output inline so the implicitly shard accessed collections override won't drop the
        // output collection during the active transaction test case, which would hang indefinitely
        // waiting for a database exclusive lock.
        {mapReduce: collName, map: function() {}, reduce: function(key, vals) {}, out: {inline: 1}},
    ];

    // There is no applyOps command on mongers.
    if (!isMongers) {
        commands.push(
            {applyOps: [{op: "u", ns: testColl.getFullName(), o2: {_id: 0}, o: {$set: {a: 5}}}]});
    }

    commands.forEach(testCommand);

    //
    // Test that doTxn is not allowed at positions after the first in transactions.
    //

    // There is no doTxn command on mongers.
    if (!isMongers) {
        assert.commandWorked(sessionDb.runCommand({
            find: collName,
            readConcern: {level: "snapshot"},
            txnNumber: NumberLong(++txnNumber),
            stmtId: NumberInt(0),
            startTransaction: true,
            autocommit: false
        }));
        assert.commandFailedWithCode(sessionDb.runCommand({
            doTxn: [{op: "u", ns: testColl.getFullName(), o2: {_id: 0}, o: {$set: {a: 5}}}],
            txnNumber: NumberLong(txnNumber),
            stmtId: NumberInt(1),
            autocommit: false
        }),
                                     ErrorCodes.OperationNotSupportedInTransaction);

        // It is still possible to commit the transaction. The rejected command does not abort the
        // transaction.
        assert.commandWorked(sessionDb.adminCommand({
            commitTransaction: 1,
            txnNumber: NumberLong(txnNumber),
            stmtId: NumberInt(2),
            autocommit: false
        }));
    }

    //
    // Test that a find command with the read-once cursor option is not allowed in a transaction.
    //
    assert.commandFailedWithCode(sessionDb.runCommand({
        find: collName,
        readOnce: true,
        readConcern: {level: "snapshot"},
        txnNumber: NumberLong(++txnNumber),
        stmtId: NumberInt(0),
        startTransaction: true,
        autocommit: false
    }),
                                 ErrorCodes.OperationNotSupportedInTransaction);

    // Mongers has special handling for commitTransaction to support commit recovery.
    if (!isMongers) {
        // The failed find should abort the transaction so a commit should fail.
        assert.commandFailedWithCode(sessionDb.adminCommand({
            commitTransaction: 1,
            autocommit: false,
            txnNumber: NumberLong(txnNumber),
            stmtId: NumberInt(1),
        }),
                                     ErrorCodes.NoSuchTransaction);
    }

    session.endSession();
}());
