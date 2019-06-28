(function() {
    'use strict';

    // TODO: Remove 'shardAsReplicaSet: false' when SERVER-32672 is fixed.
    var st = new ShardingTest({
        shards: 2,
        other: {
            chunkSize: 1,
            useHostname: true,
            keyFile: 'jstests/libs/key1',
            shardAsReplicaSet: false
        },
    });

    var mongers = st.s;
    var adminDB = mongers.getDB('admin');
    var db = mongers.getDB('test');

    adminDB.createUser({user: 'admin', pwd: 'password', roles: jsTest.adminUserRoles});

    jsTestLog("Add user was successful");

    // Test for SERVER-6549, make sure that repeatedly logging in always passes.
    for (var i = 0; i < 100; i++) {
        adminDB = new Mongo(mongers.host).getDB('admin');
        assert(adminDB.auth('admin', 'password'), "Auth failed on attempt #: " + i);
    }

    st.stop();
})();
