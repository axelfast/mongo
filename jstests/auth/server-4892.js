/**
 * Regression test for SERVER-4892.
 *
 * Verify that a client can delete cursors that it creates, when mongerd is running with "auth"
 * enabled.
 *
 * This test requires users to persist across a restart.
 * @tags: [requires_persistence]
 */

var baseName = 'jstests_auth_server4892';
var dbpath = MongoRunner.dataPath + baseName;
resetDbpath(dbpath);
var mongerdCommonArgs = {
    dbpath: dbpath,
    noCleanData: true,
};

/*
 * Start an instance of mongerd, pass it as a parameter to operation(), then stop the instance of
 * mongerd before unwinding or returning out of with_mongerd().
 *
 * 'extraMongodArgs' are extra arguments to pass on the mongerd command line, as an object.
 */
function withMongod(extraMongodArgs, operation) {
    var mongerd = MongoRunner.runMongod(Object.merge(mongerdCommonArgs, extraMongodArgs));

    try {
        operation(mongerd);
    } finally {
        MongoRunner.stopMongod(mongerd);
    }
}

/*
 * Fail an assertion if the given "mongerd" instance does not have exactly expectNumLiveCursors live
 * cursors on the server.
 */
function expectNumLiveCursors(mongerd, expectedNumLiveCursors) {
    var conn = new Mongo(mongerd.host);
    var db = mongerd.getDB('admin');
    db.auth('admin', 'admin');
    var actualNumLiveCursors = db.serverStatus().metrics.cursor.open.total;
    assert(actualNumLiveCursors == expectedNumLiveCursors,
           "actual num live cursors (" + actualNumLiveCursors + ") != exptected (" +
               expectedNumLiveCursors + ")");
}

withMongod({noauth: ""}, function setupTest(mongerd) {
    var admin, somedb, conn;
    conn = new Mongo(mongerd.host);
    admin = conn.getDB('admin');
    somedb = conn.getDB('somedb');
    admin.createUser({user: 'admin', pwd: 'admin', roles: jsTest.adminUserRoles});
    admin.auth('admin', 'admin');
    somedb.createUser({user: 'frim', pwd: 'fram', roles: jsTest.basicUserRoles});
    somedb.data.drop();
    for (var i = 0; i < 10; ++i) {
        assert.writeOK(somedb.data.insert({val: i}));
    }
    admin.logout();
});

withMongod({auth: ""}, function runTest(mongerd) {
    var conn = new Mongo(mongerd.host);
    var somedb = conn.getDB('somedb');
    somedb.auth('frim', 'fram');

    expectNumLiveCursors(mongerd, 0);

    var cursor = somedb.data.find({}, {'_id': 1}).batchSize(1);
    cursor.next();
    expectNumLiveCursors(mongerd, 1);

    cursor.close();

    // NOTE(schwerin): dbKillCursors gets piggybacked on subsequent messages on the
    // connection, so we
    // have to force a message to the server.
    somedb.data.findOne();

    expectNumLiveCursors(mongerd, 0);
});
