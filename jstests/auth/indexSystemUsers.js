// SERVER-8802: Test that you can't build indexes on system.users and use that to drop users with
// dropDups.
var conn = MongerRunner.runMongerd({auth: ""});

var adminDB = conn.getDB("admin");
var testDB = conn.getDB("test");
adminDB.createUser({user: 'admin', pwd: 'x', roles: ['userAdminAnyDatabase']});
adminDB.auth('admin', 'x');
adminDB.createUser({user: 'mallory', pwd: 'x', roles: ['readWriteAnyDatabase']});
testDB.createUser({user: 'user', pwd: 'x', roles: ['read']});
assert.eq(3, adminDB.system.users.count());
adminDB.logout();

adminDB.auth('mallory', 'x');
var res = adminDB.system.users.createIndex({haxx: 1}, {unique: true, dropDups: true});
assert(!res.ok);
assert.eq(13, res.code);  // unauthorized
// Make sure that no indexes were built.
var collectionInfosCursor = adminDB.runCommand("listCollections", {
    filter: {
        $and: [
            {name: /^admin\.system\.users\.\$/},
            {name: {$ne: "admin.system.users.$_id_"}},
            {name: {$ne: "admin.system.users.$user_1_db_1"}}
        ]
    }
});

assert.eq([], new DBCommandCursor(adminDB, collectionInfosCursor).toArray());
adminDB.logout();

adminDB.auth('admin', 'x');
// Make sure that no users were actually dropped
assert.eq(3, adminDB.system.users.count());
MongerRunner.stopMongerd(conn, null, {user: 'admin', pwd: 'x'});
