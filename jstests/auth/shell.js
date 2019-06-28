// Authenticate to a mongerd from the shell via command line.

(function() {
    'use strict';

    const port = allocatePort();
    const mongerd = MongerRunner.runMongerd({auth: '', port: port});
    const admin = mongerd.getDB('admin');

    admin.createUser({user: 'admin', pwd: 'pass', roles: jsTest.adminUserRoles});

    // Connect via shell round-trip in order to verify handling of mongerdb:// uri with password.
    const uri = 'mongerdb://admin:pass@localhost:' + port + '/admin';
    // Be sure to actually do something requiring authentication.
    const monger = runMongerProgram('monger', uri, '--eval', 'db.system.users.find({});');
    assert.eq(monger, 0, "Failed connecting to mongerd via shell+mongerdb uri");

    MongerRunner.stopMongerd(mongerd);
})();
