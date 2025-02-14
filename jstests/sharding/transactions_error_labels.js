// Test TransientTransactionErrors error label in mongers write commands.
// @tags: [uses_transactions, uses_multi_shard_transaction]
(function() {
    "use strict";

    load("jstests/sharding/libs/sharded_transactions_helpers.js");

    const dbName = "test";
    const collName = "foo";
    const ns = dbName + "." + collName;

    const failCommandWithError = function(rst, {commandToFail, errorCode, closeConnection}) {
        rst.nodes.forEach(function(node) {
            assert.commandWorked(node.getDB("admin").runCommand({
                configureFailPoint: "failCommand",
                mode: "alwaysOn",
                data: {
                    closeConnection: closeConnection,
                    errorCode: errorCode,
                    failCommands: [commandToFail],
                    failInternalCommands: true  // mongerd sees mongers as an internal client
                }
            }));
        });
    };

    const failCommandWithWriteConcernError = function(rst, commandToFail) {
        rst.nodes.forEach(function(node) {
            assert.commandWorked(node.getDB("admin").runCommand({
                configureFailPoint: "failCommand",
                mode: "alwaysOn",
                data: {
                    writeConcernError: {code: NumberInt(12345), errmsg: "dummy"},
                    failCommands: [commandToFail],
                    failInternalCommands: true  // mongerd sees mongers as an internal client
                }
            }));
        });
    };

    const turnOffFailCommand = function(rst) {
        rst.nodes.forEach(function(node) {
            assert.commandWorked(
                node.getDB("admin").runCommand({configureFailPoint: "failCommand", mode: "off"}));
        });
    };

    let numCalls = 0;
    const startTransaction = function(mongersSession, dbName, collName) {
        numCalls++;
        mongersSession.startTransaction();
        return mongersSession.getDatabase(dbName).runCommand({
            insert: collName,
            // Target both chunks, wherever they may be
            documents: [{_id: -1 * numCalls}, {_id: numCalls}],
            readConcern: {level: "snapshot"},
        });
    };

    const abortTransactionDirectlyOnParticipant = function(rst, lsid, txnNumber) {
        assert.commandWorked(rst.getPrimary().adminCommand({
            abortTransaction: 1,
            lsid: lsid,
            txnNumber: NumberLong(txnNumber),
            autocommit: false,
        }));
    };

    const commitTransaction = function(mongersSession) {
        let res = mongersSession.commitTransaction_forTesting();
        print("commitTransaction response from mongers: " + tojson(res));
        return res;
    };

    const checkMongersResponse = function(
        res, expectedErrorCode, expectedErrorLabel, writeConcernErrorExpected) {
        if (expectedErrorCode) {
            assert.eq(0, res.ok, tojson(res));
            assert.eq(expectedErrorCode, res.code, tojson(res));
        } else {
            assert.eq(1, res.ok, tojson(res));
        }

        if (expectedErrorLabel) {
            assert.neq(null, res.errorLabels, tojson(res));
            assert.contains(expectedErrorLabel, res.errorLabels, tojson(res));
        } else {
            assert.eq(null, res.errorLabels, tojson(res));
        }

        if (writeConcernErrorExpected) {
            assert.neq(null, res.writeConcernError, tojson(res));
        } else {
            assert.eq(null, res.writeConcernError, tojson(res));
        }
    };

    const runCommitTests = function(commandSentToShard) {
        jsTest.log("Mongers does not attach any error label if " + commandSentToShard +
                   " returns success.");
        assert.commandWorked(startTransaction(mongersSession, dbName, collName));
        res = mongersSession.commitTransaction_forTesting();
        checkMongersResponse(res, null, null, null);

        jsTest.log("Mongers does not attach any error label if " + commandSentToShard +
                   " returns success with writeConcern error.");
        failCommandWithWriteConcernError(st.rs0, commandSentToShard);
        assert.commandWorked(startTransaction(mongersSession, dbName, collName));
        res = mongersSession.commitTransaction_forTesting();
        checkMongersResponse(res, null, null, true);
        turnOffFailCommand(st.rs0);

        jsTest.log("Mongers attaches 'TransientTransactionError' label if " + commandSentToShard +
                   " returns NoSuchTransaction.");
        assert.commandWorked(startTransaction(mongersSession, dbName, collName));
        abortTransactionDirectlyOnParticipant(
            st.rs0, mongersSession.getSessionId(), mongersSession.getTxnNumber_forTesting());
        res = mongersSession.commitTransaction_forTesting();
        checkMongersResponse(res, ErrorCodes.NoSuchTransaction, "TransientTransactionError", null);
        turnOffFailCommand(st.rs0);

        jsTest.log("Mongers does not attach any error label if " + commandSentToShard +
                   " returns NoSuchTransaction with writeConcern error.");
        failCommandWithWriteConcernError(st.rs0, commandSentToShard);
        assert.commandWorked(startTransaction(mongersSession, dbName, collName));
        abortTransactionDirectlyOnParticipant(
            st.rs0, mongersSession.getSessionId(), mongersSession.getTxnNumber_forTesting());
        res = mongersSession.commitTransaction_forTesting();
        checkMongersResponse(res, ErrorCodes.NoSuchTransaction, null, true);
        turnOffFailCommand(st.rs0);

        jsTest.log("No error label for network error if " + commandSentToShard +
                   " returns network error");
        assert.commandWorked(startTransaction(mongersSession, dbName, collName));
        failCommandWithError(st.rs0, {
            commandToFail: commandSentToShard,
            errorCode: ErrorCodes.InternalError,
            closeConnection: true
        });
        res = mongersSession.commitTransaction_forTesting();
        checkMongersResponse(res, ErrorCodes.HostUnreachable, false /* expectedErrorLabel */, null);
        turnOffFailCommand(st.rs0);
    };

    let st = new ShardingTest({shards: 2, config: 1, mongersOptions: {verbose: 3}});

    // Create a sharded collection with a chunk on each shard:
    // shard0: [-inf, 0)
    // shard1: [0, +inf)
    assert.commandWorked(st.s.adminCommand({enableSharding: dbName}));
    assert.commandWorked(st.s.adminCommand({movePrimary: dbName, to: st.shard0.shardName}));
    assert.commandWorked(st.s.adminCommand({shardCollection: ns, key: {_id: 1}}));
    assert.commandWorked(st.s.adminCommand({split: ns, middle: {_id: 0}}));

    // These forced refreshes are not strictly necessary; they just prevent extra TXN log lines
    // from the shards starting, aborting, and restarting the transaction due to needing to
    // refresh after the transaction has started.
    assert.commandWorked(st.shard0.adminCommand({_flushRoutingTableCacheUpdates: ns}));
    assert.commandWorked(st.shard1.adminCommand({_flushRoutingTableCacheUpdates: ns}));

    let mongersSession = st.s.startSession();
    let mongersSessionDB = mongersSession.getDatabase(dbName);

    let res;

    // write statement
    jsTest.log(
        "'TransientTransactionError' label is attached if write statement returns WriteConflict");
    failCommandWithError(
        st.rs0,
        {commandToFail: "insert", errorCode: ErrorCodes.WriteConflict, closeConnection: false});
    res = startTransaction(mongersSession, dbName, collName);
    checkMongersResponse(res, ErrorCodes.WriteConflict, "TransientTransactionError", null);
    turnOffFailCommand(st.rs0);
    assert.commandFailedWithCode(mongersSession.abortTransaction_forTesting(),
                                 ErrorCodes.NoSuchTransaction);

    // statements prior to commit network error
    failCommandWithError(
        st.rs0,
        {commandToFail: "insert", errorCode: ErrorCodes.InternalError, closeConnection: true});
    res = startTransaction(mongersSession, dbName, collName);
    checkMongersResponse(res, ErrorCodes.HostUnreachable, "TransientTransactionError", null);
    turnOffFailCommand(st.rs0);
    assert.commandFailedWithCode(mongersSession.abortTransaction_forTesting(),
                                 ErrorCodes.NoSuchTransaction);

    // commitTransaction for single-shard transaction (mongers sends commitTransaction)
    runCommitTests("commitTransaction");

    // commitTransaction for multi-shard transaction (mongers sends coordinateCommitTransaction)
    assert.commandWorked(
        st.s.adminCommand({moveChunk: ns, find: {_id: 0}, to: st.shard1.shardName}));
    flushRoutersAndRefreshShardMetadata(st, {ns});
    runCommitTests("coordinateCommitTransaction");

    st.stop();
}());
