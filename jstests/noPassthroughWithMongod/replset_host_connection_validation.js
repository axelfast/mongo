// Test --host with a replica set.
// @tags: [requires_replication]

(function() {
    'use strict';

    const replSetName = 'hostTestReplSetName';

    // This "inner_mode" method of spawning a replset and re-running was copied from
    // host_connection_string_validation.js
    if ("undefined" == typeof inner_mode) {
        jsTest.log("Outer mode test starting a replica set");

        const replTest = new ReplSetTest({name: replSetName, nodes: 2});
        replTest.startSet();
        replTest.initiate();

        const primary = replTest.getPrimary();

        const args = [
            "monger",
            "--nodb",
            "--eval",
            "inner_mode=true;port=" + primary.port + ";",
            "jstests/noPassthroughWithMongerd/replset_host_connection_validation.js"
        ];
        const exitCode = _runMongerProgram(...args);
        jsTest.log("Inner mode test finished, exit code was " + exitCode);

        replTest.stopSet();
        // Pass the inner test's exit code back as the outer test's exit code
        if (exitCode != 0) {
            doassert("inner test failed with exit code " + exitCode);
        }
        return;
    }

    function testHost(host, uri, ok) {
        const exitCode = runMongerProgram('monger', '--eval', ';', '--host', host, uri);
        if (ok) {
            assert.eq(exitCode, 0, "failed to connect with `--host " + host + "`");
        } else {
            assert.neq(exitCode, 0, "unexpectedly succeeded to connect with `--host " + host + "`");
        }
    }

    function runConnectionStringTestFor(connectionString, uri, ok) {
        print("* Testing: --host " + connectionString + " " + uri);
        if (!ok) {
            print("  This should fail");
        }
        testHost(connectionString, uri, ok);
    }

    function expSuccess(str) {
        runConnectionStringTestFor(str, '', true);
        if (!str.startsWith('mongerdb://')) {
            runConnectionStringTestFor(str, 'dbname', true);
        }
    }

    function expFailure(str) {
        runConnectionStringTestFor(str, '', false);
    }

    expSuccess(`localhost:${port}`);
    expSuccess(`${replSetName}/localhost:${port}`);
    expSuccess(`${replSetName}/localhost:${port},[::1]:${port}`);
    expSuccess(`${replSetName}/localhost:${port},`);
    expSuccess(`${replSetName}/localhost:${port},,`);
    expSuccess(`mongerdb://localhost:${port}/admin?replicaSet=${replSetName}`);
    expSuccess(`mongerdb://localhost:${port}`);

    expFailure(',');
    expFailure(',,');
    expFailure(`${replSetName}/`);
    expFailure(`${replSetName}/,`);
    expFailure(`${replSetName}/,,`);
    expFailure(`${replSetName}//not/a/socket`);
    expFailure(`mongerdb://localhost:${port}/admin?replicaSet=`);
    expFailure('mongerdb://localhost:');
    expFailure(`mongerdb://:${port}`);

    jsTest.log("SUCCESSFUL test completion");
})();
