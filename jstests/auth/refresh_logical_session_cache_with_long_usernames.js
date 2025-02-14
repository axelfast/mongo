// Verifies that we've fixed SERVER-33158 by creating large user lsid refresh records (via large
// usernames)

(function() {
    'use strict';

    // This test makes assertions about the number of sessions, which are not compatible with
    // implicit sessions.
    TestData.disableImplicitSessions = true;

    const mongerd = MongerRunner.runMongerd({auth: ""});

    const refresh = {refreshLogicalSessionCacheNow: 1};
    const startSession = {startSession: 1};

    const admin = mongerd.getDB('admin');
    const db = mongerd.getDB("test");
    const config = mongerd.getDB("config");

    admin.createUser({user: 'admin', pwd: 'pass', roles: jsTest.adminUserRoles});
    assert(admin.auth('admin', 'pass'));

    const longUserName = "x".repeat(1000);

    // Create a user with a long name, so that the refresh records have a chance to blow out the
    // 16MB limit, if all the sessions are flushed in one batch
    db.createUser({user: longUserName, pwd: 'pass', roles: jsTest.basicUserRoles});
    admin.logout();

    assert(db.auth(longUserName, 'pass'));

    // 20k * 1k = 20mb which is greater than 16mb
    const numSessions = 20000;
    for (var i = 0; i < numSessions; i++) {
        assert.commandWorked(admin.runCommand(startSession), "unable to start session");
    }

    assert.commandWorked(admin.runCommand(refresh), "failed to refresh");

    // Make sure we actually flushed the sessions
    assert.eq(numSessions,
              config.system.sessions.aggregate([{'$listSessions': {}}, {'$count': "count"}])
                  .next()
                  .count);

    MongerRunner.stopMongerd(mongerd);
})();
