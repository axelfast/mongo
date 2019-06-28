/**
 * Tests that the monger shell retries exactly once on retryable errors.
 *
 * @tags: [requires_replication]
 */
(function() {
    "use strict";

    load("jstests/libs/retryable_writes_util.js");

    if (!RetryableWritesUtil.storageEngineSupportsRetryableWrites(jsTest.options().storageEngine)) {
        jsTestLog("Retryable writes are not supported, skipping test");
        return;
    }

    const rst = new ReplSetTest({nodes: 2});
    rst.startSet();
    rst.initiate();

    const dbName = "test";
    const collName = jsTest.name();

    const rsConn = new Monger(rst.getURL());
    const db = rsConn.startSession({retryWrites: true}).getDatabase(dbName);

    // We configure the monger shell to log its retry attempts so there are more diagnostics
    // available in case this test ever fails.
    TestData.logRetryAttempts = true;

    /**
     * The testCommandIsRetried() function serves as the fixture for writing test cases which run
     * commands against the server and assert that the monger shell retries them correctly.
     *
     * The 'testFn' parameter is a function that performs an arbitrary number of operations against
     * the database. The command requests that the monger shell attempts to send to the server
     * (including any command requests which are retried) are then specified as the sole argument to
     * the 'assertFn' parameter.
     *
     * The testFn(enableCapture, disableCapture) function can also selectively turn on and off the
     * capturing of command requests by calling the functions it receives for its first and second
     * parameters, respectively.
     */
    function testCommandIsRetried(testFn, assertFn) {
        const mongerRunCommandOriginal = Monger.prototype.runCommand;
        const cmdObjsSeen = [];

        let shouldCaptureCmdObjs = true;

        Monger.prototype.runCommand = function runCommandSpy(dbName, cmdObj, options) {
            if (shouldCaptureCmdObjs) {
                cmdObjsSeen.push(cmdObj);
            }

            return mongerRunCommandOriginal.apply(this, arguments);
        };

        try {
            assert.doesNotThrow(() => testFn(
                                    () => {
                                        shouldCaptureCmdObjs = true;
                                    },
                                    () => {
                                        shouldCaptureCmdObjs = false;
                                    }));
        } finally {
            Monger.prototype.runCommand = mongerRunCommandOriginal;
        }

        if (cmdObjsSeen.length === 0) {
            throw new Error("Monger.prototype.runCommand() was never called: " + testFn.toString());
        }

        assertFn(cmdObjsSeen);
    }

    testCommandIsRetried(
        function testInsertRetriedOnWriteConcernError(enableCapture, disableCapture) {
            disableCapture();
            const secondary = rst.getSecondary();
            secondary.adminCommand({configureFailPoint: "rsSyncApplyStop", mode: "alwaysOn"});

            try {
                enableCapture();
                const res = db[collName].insert({}, {writeConcern: {w: 2, wtimeout: 1000}});
                assert.commandFailedWithCode(res, ErrorCodes.WriteConcernFailed);
                disableCapture();
            } finally {
                // We disable the failpoint in a finally block to prevent a misleading fassert()
                // message from being logged by the secondary when it is shut down with the
                // failpoint enabled.
                secondary.adminCommand({configureFailPoint: "rsSyncApplyStop", mode: "off"});
            }
        },
        function assertInsertRetriedExactlyOnce(cmdObjsSeen) {
            assert.eq(2, cmdObjsSeen.length, () => tojson(cmdObjsSeen));
            assert(cmdObjsSeen.every(cmdObj => Object.keys(cmdObj)[0] === "insert"),
                   () => "expected both attempts to be insert requests: " + tojson(cmdObjsSeen));
            assert.eq(
                cmdObjsSeen[0], cmdObjsSeen[1], "command request changed between retry attempts");
        });

    testCommandIsRetried(
        function testUpdateRetriedOnRetryableCommandError(enableCapture, disableCapture) {
            disableCapture();

            const primary = rst.getPrimary();
            primary.adminCommand({
                configureFailPoint: "onPrimaryTransactionalWrite",
                data: {
                    closeConnection: false,
                    failBeforeCommitExceptionCode: ErrorCodes.InterruptedDueToReplStateChange
                },
                mode: {times: 1}
            });

            enableCapture();
            const res = db[collName].update({}, {$set: {a: 1}});
            assert.commandWorked(res);
            disableCapture();

            primary.adminCommand({configureFailPoint: "onPrimaryTransactionalWrite", mode: "off"});
        },
        function assertUpdateRetriedExactlyOnce(cmdObjsSeen) {
            assert.eq(2, cmdObjsSeen.length, () => tojson(cmdObjsSeen));
            assert(cmdObjsSeen.every(cmdObj => Object.keys(cmdObj)[0] === "update"),
                   () => "expected both attempts to be update requests: " + tojson(cmdObjsSeen));
            assert.eq(
                cmdObjsSeen[0], cmdObjsSeen[1], "command request changed between retry attempts");
        });

    rst.stopSet();
})();
