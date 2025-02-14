/*

Exhaustive test for authorization of commands with builtin roles.

The test logic implemented here operates on the test cases defined
in jstests/auth/commands.js.

@tags: [requires_sharding]

*/

// TODO SERVER-35447: This test involves killing all sessions, which will not work as expected if
// the kill command is sent with an implicit session.
TestData.disableImplicitSessions = true;

load("jstests/auth/lib/commands_lib.js");

var roles = [
    {key: "read", role: "read", dbname: firstDbName},
    {key: "readLocal", role: {role: "read", db: "local"}, dbname: adminDbName},
    {key: "readAnyDatabase", role: "readAnyDatabase", dbname: adminDbName},
    {key: "readWrite", role: "readWrite", dbname: firstDbName},
    {key: "readWriteLocal", role: {role: "readWrite", db: "local"}, dbname: adminDbName},
    {key: "readWriteAnyDatabase", role: "readWriteAnyDatabase", dbname: adminDbName},
    {key: "userAdmin", role: "userAdmin", dbname: firstDbName},
    {key: "userAdminAnyDatabase", role: "userAdminAnyDatabase", dbname: adminDbName},
    {key: "dbAdmin", role: "dbAdmin", dbname: firstDbName},
    {key: "dbAdminAnyDatabase", role: "dbAdminAnyDatabase", dbname: adminDbName},
    {key: "clusterAdmin", role: "clusterAdmin", dbname: adminDbName},
    {key: "dbOwner", role: "dbOwner", dbname: firstDbName},
    {key: "enableSharding", role: "enableSharding", dbname: firstDbName},
    {key: "clusterMonitor", role: "clusterMonitor", dbname: adminDbName},
    {key: "hostManager", role: "hostManager", dbname: adminDbName},
    {key: "clusterManager", role: "clusterManager", dbname: adminDbName},
    {key: "backup", role: "backup", dbname: adminDbName},
    {key: "restore", role: "restore", dbname: adminDbName},
    {key: "root", role: "root", dbname: adminDbName},
    {key: "__system", role: "__system", dbname: adminDbName}
];

/**
 * Parameters:
 *   conn -- connection, either to standalone mongerd,
 *      or to mongers in sharded cluster
 *   t -- a test object from the tests array in jstests/auth/commands.js
 *   testcase -- the particular testcase from t to test
 *   r -- a role object from the "roles" array above
 *
 * Returns:
 *   An empty string on success, or an error string
 *   on test failure.
 */
function testProperAuthorization(conn, t, testcase, r) {
    var out = "";

    var runOnDb = conn.getDB(testcase.runOnDb);
    var state = authCommandsLib.setup(conn, t, runOnDb);
    assert(r.db.auth("user|" + r.key, "password"));
    authCommandsLib.authenticatedSetup(t, runOnDb);
    var command = t.command;
    if (typeof(command) === "function") {
        command = t.command(state, testcase.commandArgs);
    }
    var res = runOnDb.runCommand(command);

    if (testcase.roles[r.key]) {
        if (res.ok == 0 && res.code == authErrCode) {
            out = "expected authorization success" + " but received " + tojson(res) + " on db " +
                testcase.runOnDb + " with role " + r.key;
        } else if (res.ok == 0 && !testcase.expectFail && res.code != commandNotSupportedCode) {
            // don't error if the test failed with code commandNotSupported since
            // some storage engines (e.g wiredTiger) don't support some commands (e.g. touch)
            out = "command failed with " + tojson(res) + " on db " + testcase.runOnDb +
                " with role " + r.key;
        }
    } else {
        if (res.ok == 1 || (res.ok == 0 && res.code != authErrCode)) {
            out = "expected authorization failure" + " but received result " + tojson(res) +
                " on db " + testcase.runOnDb + " with role " + r.key;
        }
    }

    r.db.logout();
    authCommandsLib.teardown(conn, t, runOnDb, res);
    return out;
}

function runOneTest(conn, t) {
    var failures = [];

    for (var i = 0; i < t.testcases.length; i++) {
        var testcase = t.testcases[i];
        if (!("roles" in testcase)) {
            continue;
        }
        for (var j = 0; j < roles.length; j++) {
            var msg = testProperAuthorization(conn, t, testcase, roles[j]);
            if (msg) {
                failures.push(t.testname + ": " + msg);
            }
        }
    }

    return failures;
}

function createUsers(conn) {
    var adminDb = conn.getDB(adminDbName);
    adminDb.createUser({user: "admin", pwd: "password", roles: ["__system"]});

    assert(adminDb.auth("admin", "password"));
    for (var i = 0; i < roles.length; i++) {
        r = roles[i];
        r.db = conn.getDB(r.dbname);
        r.db.createUser({user: "user|" + r.key, pwd: "password", roles: [r.role]});
    }
    adminDb.logout();
}

/*
 * Makes sure that none of the test cases reference roles
 * that aren't part of the global "roles" array.
 */
function checkForNonExistentRoles() {
    var tests = authCommandsLib.tests;
    for (var i = 0; i < tests.length; i++) {
        var test = tests[i];
        for (var j = 0; j < test.testcases.length; j++) {
            var testcase = test.testcases[j];
            for (role in testcase.roles) {
                var roleExists = false;
                for (var k = 0; k < roles.length; k++) {
                    if (roles[k].key === role) {
                        roleExists = true;
                        break;
                    }
                }
                assert(roleExists,
                       "Role " + role + " found in test: " + test.testname +
                           ", but doesn't exist in roles array");
            }
        }
    }
}

const dbPath = MongerRunner.toRealDir("$dataDir/commands_built_in_roles/");
mkdir(dbPath);
var opts = {
    auth: "",
    enableExperimentalStorageDetailsCmd: "",
    setParameter: "trafficRecordingDirectory=" + dbPath
};
var impls = {createUsers: createUsers, runOneTest: runOneTest};

checkForNonExistentRoles();

// run all tests standalone
var conn = MongerRunner.runMongerd(opts);
authCommandsLib.runTests(conn, impls);
MongerRunner.stopMongerd(conn);

// run all tests sharded
// TODO: Remove 'shardAsReplicaSet: false' when SERVER-32672 is fixed.
conn = new ShardingTest({
    shards: 2,
    mongers: 1,
    keyFile: "jstests/libs/key1",
    other: {
        shardOptions: opts,
        shardAsReplicaSet: false,
        mongersOptions: {setParameter: "trafficRecordingDirectory=" + dbPath}
    }
});
authCommandsLib.runTests(conn, impls);
conn.stop();
