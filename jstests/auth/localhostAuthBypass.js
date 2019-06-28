// SERVER-6591: Localhost authentication exception doesn't work right on sharded cluster
//
// This test is to ensure that localhost authentication works correctly against a standalone
// mongerd whether it is hosted with "localhost" or a hostname.

var baseName = "auth_server-6591";
var dbpath = MongoRunner.dataPath + baseName;
var username = "foo";
var password = "bar";

load("jstests/libs/host_ipaddr.js");

var createUser = function(db) {
    print("============ adding a user.");
    db.createUser({user: username, pwd: password, roles: jsTest.adminUserRoles});
};

var createRole = function(monger) {
    print("============ adding a role.");
    monger.getDB("admin").createRole(
        {role: "roleAdministrator", roles: [{role: "userAdmin", db: "admin"}], privileges: []});
};

var assertCannotRunCommands = function(monger) {
    print("============ ensuring that commands cannot be run.");

    var test = monger.getDB("test");
    assert.throws(function() {
        test.system.users.findOne();
    });

    assert.writeError(test.foo.save({_id: 0}));

    assert.throws(function() {
        test.foo.findOne({_id: 0});
    });

    assert.writeError(test.foo.update({_id: 0}, {$set: {x: 20}}));
    assert.writeError(test.foo.remove({_id: 0}));

    assert.throws(function() {
        test.foo.mapReduce(
            function() {
                emit(1, 1);
            },
            function(id, count) {
                return Array.sum(count);
            },
            {out: "other"});
    });

    // Additional commands not permitted
    // Create non-admin user
    assert.throws(function() {
        monger.getDB("test").createUser({user: username, pwd: password, roles: ['readWrite']});
    });
    // Create collection
    var authorizeErrorCode = 13;
    assert.commandFailedWithCode(
        monger.getDB("test").createCollection("log", {capped: true, size: 5242880, max: 5000}),
        authorizeErrorCode,
        "createCollection");
    // Set/Get system parameters
    var params = [
        {param: "journalCommitInterval", val: 200},
        {param: "logLevel", val: 2},
        {param: "logUserIds", val: 1},
        {param: "notablescan", val: 1},
        {param: "quiet", val: 1},
        {param: "replApplyBatchSize", val: 10},
        {param: "replIndexPrefetch", val: "none"},
        {param: "syncdelay", val: 30},
        {param: "traceExceptions", val: true},
        {param: "sslMode", val: "preferSSL"},
        {param: "clusterAuthMode", val: "sendX509"},
        {param: "userCacheInvalidationIntervalSecs", val: 300}
    ];
    params.forEach(function(p) {
        var cmd = {setParameter: 1};
        cmd[p.param] = p.val;
        assert.commandFailedWithCode(
            monger.getDB("admin").runCommand(cmd), authorizeErrorCode, "setParameter: " + p.param);
    });
    params.forEach(function(p) {
        var cmd = {getParameter: 1};
        cmd[p.param] = 1;
        assert.commandFailedWithCode(
            monger.getDB("admin").runCommand(cmd), authorizeErrorCode, "getParameter: " + p.param);
    });
};

var assertCanRunCommands = function(monger) {
    print("============ ensuring that commands can be run.");

    var test = monger.getDB("test");
    // will throw on failure
    test.system.users.findOne();

    assert.writeOK(test.foo.save({_id: 0}));
    assert.writeOK(test.foo.update({_id: 0}, {$set: {x: 20}}));
    assert.writeOK(test.foo.remove({_id: 0}));

    test.foo.mapReduce(
        function() {
            emit(1, 1);
        },
        function(id, count) {
            return Array.sum(count);
        },
        {out: "other"});
};

var authenticate = function(monger) {
    print("============ authenticating user.");
    monger.getDB("admin").auth(username, password);
};

var shutdown = function(conn) {
    print("============ shutting down.");
    MongoRunner.stopMongod(conn, /*signal*/ false, {auth: {user: username, pwd: password}});
};

var runTest = function(useHostName, useSession) {
    print("==========================");
    print("starting mongerd: useHostName=" + useHostName);
    print("==========================");
    var conn = MongoRunner.runMongod({auth: "", dbpath: dbpath, useHostName: useHostName});

    var monger = new Mongo("localhost:" + conn.port);

    assertCannotRunCommands(monger);

    if (useSession) {
        var session = monger.startSession();
        createUser(session.getDatabase("admin"));
        session.endSession();
    } else {
        createUser(monger.getDB("admin"));
    }

    assertCannotRunCommands(monger);

    authenticate(monger);

    assertCanRunCommands(monger);

    print("============ reconnecting with new client.");
    monger = new Mongo("localhost:" + conn.port);

    assertCannotRunCommands(monger);

    authenticate(monger);

    assertCanRunCommands(monger);

    shutdown(conn);
};

var runNonlocalTest = function(host) {
    print("==========================");
    print("starting mongerd: non-local host access " + host);
    print("==========================");
    var conn = MongoRunner.runMongod({auth: "", dbpath: dbpath});

    var monger = new Mongo(host + ":" + conn.port);

    assertCannotRunCommands(monger);
    assert.throws(function() {
        monger.getDB("admin").createUser(
            {user: username, pwd: password, roles: jsTest.adminUserRoles});
    });
    assert.throws(function() {
        monger.getDB("$external")
            .createUser({user: username, pwd: password, roles: jsTest.adminUserRoles});
    });
    shutdown(conn);
};

// Per SERVER-23503, the existence of roles in the admin database should disable the localhost
// exception.
// Start the server without auth. Create a role. Restart the server with auth. The exception is
// now enabled.
var runRoleTest = function() {
    var conn = MongoRunner.runMongod({dbpath: dbpath});
    var monger = new Mongo("localhost:" + conn.port);
    assertCanRunCommands(monger);
    createRole(monger);
    assertCanRunCommands(monger);
    MongoRunner.stopMongod(conn);
    conn = MongoRunner.runMongod({auth: '', dbpath: dbpath, restart: true, cleanData: false});
    monger = new Mongo("localhost:" + conn.port);
    assertCannotRunCommands(monger);
    MongoRunner.stopMongod(conn);
};

runTest(false, false);
runTest(false, true);
runTest(true, false);
runTest(true, true);

runNonlocalTest(get_ipaddr());

runRoleTest();
