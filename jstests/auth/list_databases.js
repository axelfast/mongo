// Auth tests for the listDatabases command.

(function() {
    'use strict';

    function runTest(mongerd) {
        const admin = mongerd.getDB('admin');
        admin.createUser({user: 'admin', pwd: 'pass', roles: jsTest.adminUserRoles});
        assert(admin.auth('admin', 'pass'));

        // Establish db0..db7
        for (let i = 0; i < 8; ++i) {
            mongerd.getDB('db' + i).foo.insert({bar: "baz"});
        }
        mongerd.getDB("db0").baz.insert({x: "y"});
        mongerd.getDB("db2").baz.insert({x: "y"});

        admin.createRole({
            role: 'dbLister',
            privileges: [{resource: {cluster: true}, actions: ['listDatabases']}],
            roles: []
        });

        admin.createRole({
            role: 'specificCollection',
            privileges: [{resource: {db: "db0", collection: "baz"}, actions: ['find']}],
            roles: []
        });

        admin.createRole({
            role: 'sharedNameCollections',
            privileges: [{resource: {db: "", collection: "baz"}, actions: ['find']}],
            roles: []
        });

        // Make db0, db2, db4, db6 readable to user1 abd user3.
        // Make db0, db1, db2, db3 read/writable to user 2 and user3.
        function makeRole(perm, dbNum) {
            return {role: perm, db: ("db" + dbNum)};
        }
        const readEven = [0, 2, 4, 6].map(function(i) {
            return makeRole("read", i);
        });
        const readWriteLow = [0, 1, 2, 3].map(function(i) {
            return makeRole("readWrite", i);
        });
        admin.createUser({user: 'user1', pwd: 'pass', roles: readEven});
        admin.createUser({user: 'user2', pwd: 'pass', roles: readWriteLow});
        admin.createUser({user: 'user3', pwd: 'pass', roles: readEven.concat(readWriteLow)});

        // Make db4 readable by user 4, and let them list all dbs.
        // Make db5 readable by user 5, and let them list all dbs.
        // Make collection baz in db0 findable by user6, and let them list db0.
        // Make all baz collections findable by user7, and let them list all dbs.
        admin.createUser({user: 'user4', pwd: 'pass', roles: [makeRole('read', 4), 'dbLister']});
        admin.createUser({user: 'user5', pwd: 'pass', roles: [makeRole('read', 5), 'dbLister']});
        admin.createUser({user: 'user6', pwd: 'pass', roles: ['specificCollection']});
        admin.createUser({user: 'user7', pwd: 'pass', roles: ['sharedNameCollections']});
        admin.logout();

        const admin_dbs = ["admin", "db0", "db1", "db2", "db3", "db4", "db5", "db6", "db7"];

        [{user: "user1", dbs: ["db0", "db2", "db4", "db6"]},
         {user: "user2", dbs: ["db0", "db1", "db2", "db3"]},
         {user: "user3", dbs: ["db0", "db1", "db2", "db3", "db4", "db6"]},
         {user: "user4", dbs: admin_dbs, authDbs: ["db4"]},
         {user: "user5", dbs: admin_dbs, authDbs: ["db5"]},
         {user: "user6", dbs: ["db0"]},
         {user: "user7", dbs: admin_dbs},
         {user: "admin", dbs: admin_dbs, authDbs: admin_dbs},
        ].forEach(function(test) {
            function filterSpecial(db) {
                // Returning of local/config varies with sharding/mobile/etc..
                // Ignore these for simplicity.
                return (db !== 'local') && (db !== 'config');
            }

            // Invoking {listDatabases: 1} directly.
            function tryList(cmd, expect_dbs) {
                const dbs = assert.commandWorked(admin.runCommand(cmd));
                assert.eq(dbs.databases
                              .map(function(db) {
                                  return db.name;
                              })
                              .filter(filterSpecial)
                              .sort(),
                          expect_dbs,
                          test.user + " permissions");
            }

            admin.auth(test.user, 'pass');
            tryList({listDatabases: 1}, test.dbs);
            tryList({listDatabases: 1, authorizedDatabases: true}, test.authDbs || test.dbs);

            if (test.authDbs) {
                tryList({listDatabases: 1, authorizedDatabases: false}, test.dbs);
            } else {
                // Users without listDatabases cluster perm may not
                // request authorizedDatabases: false.
                assert.throws(tryList, [{listDatabases: 1, authorizedDatabases: false}, test.dbs]);
            }

            // Test using shell helper Monger.getDBs().
            assert.eq(mongerd.getDBs(undefined, {}, true).filter(filterSpecial),
                      test.dbs,
                      "Shell helper speaking to same version");
            if (test.user !== 'admin' && test.user !== "user7") {
                // Admin and user7 don't have an explicit list of DBs to parse.
                assert.eq(mongerd._getDatabaseNamesFromPrivileges(), test.authDbs || test.dbs);

                // Test (non-admin) call to Monger.getDBs() on a < 4.0 MongerD
                // by injecting a command failure into Monger.adminCommand().
                // This will allow us to resemble a < 4.0 server.
                const adminCommandFunction = mongerd.adminCommand;
                const adminCommandMethod = adminCommandFunction.bind(mongerd);

                try {
                    mongerd.adminCommand = function(cmd) {
                        if (cmd.hasOwnProperty('listDatabases')) {
                            return {
                                ok: 0,
                                errmsg: 'Stubbed command failure: ' + tojson(cmd),
                                code: ErrorCodes.Unauthorized,
                                codeName: 'Unauthorized'
                            };
                        }
                        return adminCommandMethod(cmd);
                    };
                    // Command fails, but we dispatch via _getDatabaseNamesFromPrivileges().
                    assert.eq(mongerd.getDBs().databases.map(function(x) {
                        return x.name;
                    }),
                              test.authDbs || test.dbs);

                    // Still dispatches with explicit nameOnly===true, returns only names.
                    assert.eq(mongerd.getDBs(undefined, undefined, true), test.authDbs || test.dbs);

                    // Command fails and unable to dispatch because nameOnly !== true.
                    assert.throws(() => mongerd.getDBs(undefined, undefined, false));

                    // Command fails and unable to dispatch because filter is not empty.
                    assert.throws(() => mongerd.getDBs(undefined, {name: 'foo'}));
                } finally {
                    mongerd.adminCommand = adminCommandFunction;
                }
            }

            admin.logout();
        });
    }

    const mongerd = MongerRunner.runMongerd({auth: ""});
    runTest(mongerd);
    MongerRunner.stopMongerd(mongerd);

    if (jsTest.options().storageEngine !== "mobile") {
        // TODO: Remove 'shardAsReplicaSet: false' when SERVER-32672 is fixed.
        const st = new ShardingTest({
            shards: 1,
            mongers: 1,
            config: 1,
            other: {keyFile: 'jstests/libs/key1', shardAsReplicaSet: false}
        });
        runTest(st.s0);
        st.stop();
    }
})();
