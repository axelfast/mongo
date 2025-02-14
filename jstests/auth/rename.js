// test renameCollection with auth

// TODO SERVER-35447: Multiple users cannot be authenticated on one connection within a session.
TestData.disableImplicitSessions = true;

var m = MongerRunner.runMongerd({auth: ""});

var db1 = m.getDB("foo");
var db2 = m.getDB("bar");
var admin = m.getDB('admin');

// Setup initial data
admin.createUser({user: 'admin', pwd: 'password', roles: jsTest.adminUserRoles});
admin.auth('admin', 'password');

db1.createUser({user: "foo", pwd: "bar", roles: jsTest.basicUserRoles});
db2.createUser({user: "bar", pwd: "foo", roles: jsTest.basicUserRoles});

printjson(db1.a.count());
db1.a.save({});
assert.eq(db1.a.count(), 1);

admin.logout();

// can't run same db w/o auth
assert.commandFailed(
    admin.runCommand({renameCollection: db1.a.getFullName(), to: db1.b.getFullName()}));

// can run same db with auth
db1.auth('foo', 'bar');
assert.commandWorked(
    admin.runCommand({renameCollection: db1.a.getFullName(), to: db1.b.getFullName()}));

// can't run diff db w/o auth
assert.commandFailed(
    admin.runCommand({renameCollection: db1.b.getFullName(), to: db2.a.getFullName()}));

// can run diff db with auth
db2.auth('bar', 'foo');
assert.commandWorked(
    admin.runCommand({renameCollection: db1.b.getFullName(), to: db2.a.getFullName()}));

// test post conditions
assert.eq(db1.a.count(), 0);
assert.eq(db1.b.count(), 0);
assert.eq(db2.a.count(), 1);

MongerRunner.stopMongerd(m, null, {user: 'admin', pwd: 'password'});
