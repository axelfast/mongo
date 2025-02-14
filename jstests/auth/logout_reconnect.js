/*
 * This file tests that credentials are flushed
 * from the Monger shell authCache when logging out.
 * It is a regression test for SERVER-8798.
 */

var conn = MongerRunner.runMongerd({auth: "", remember: true});

// create user with rw permissions and login
var testDB = conn.getDB('test');
var adminDB = conn.getDB('admin');
adminDB.createUser({user: 'admin', pwd: 'admin', roles: ['userAdminAnyDatabase']});
adminDB.auth('admin', 'admin');
testDB.createUser({user: 'rwuser', pwd: 'rwuser', roles: ['readWrite']});
adminDB.logout();
testDB.auth('rwuser', 'rwuser');

// verify that the rwuser can read and write
testDB.foo.insert({a: 1});
assert.eq(1, testDB.foo.find({a: 1}).count(), "failed to read");

// assert that the user cannot read unauthenticated
testDB.logout();
assert.throws(function() {
    testDB.foo.findOne();
}, [], "user should not be able to read after logging out");

MongerRunner.stopMongerd(conn);
conn = MongerRunner.runMongerd({restart: conn, noCleanData: true});

// expect to fail on first attempt since the socket is no longer valid
try {
    val = testDB.foo.findOne();
} catch (err) {
}

// assert that credentials were not autosubmitted on reconnect
assert.throws(function() {
    testDB.foo.findOne();
}, [], "user should not be able to read after logging out");

MongerRunner.stopMongerd(conn);

print("SUCCESS logout_reconnect.js");
