// Tests the user cache document source

(function() {
    'use strict';

    var mongerd = MongerRunner.runMongerd({auth: ""});
    var db = mongerd.getDB("admin");
    db.createUser({user: "root", pwd: "root", roles: ["userAdminAnyDatabase"]});
    db.auth("root", "root");
    db.createUser({user: "readOnlyUser", pwd: "foobar", roles: ["readAnyDatabase"]});
    var readUserCache = function() {
        var ret = db.aggregate([{$listCachedAndActiveUsers: {}}]).toArray();
        print(tojson(ret));
        return ret;
    };

    const expectedOnlyRoot = [{username: "root", db: "admin", active: true}];
    assert.eq(expectedOnlyRoot, readUserCache());

    /* This is broken because of SERVER-36384
    var newConn = new Monger(mongerd.name);
    assert.eq(newConn.getDB("admin").auth("readOnlyUser", "foobar"), 1);

    const expectedBothActive = [
        { username: "root", db: "admin", active: true },
        { username: "readOnlyUser", db: "admin", active: true }
    ];
    assert.eq(expectedBothActive, readUserCache());

    newConn.close();
    */

    var awaitShell = startParallelShell(function() {
        assert.eq(db.getSisterDB("admin").auth("readOnlyUser", "foobar"), 1);
    }, mongerd.port);

    const expectedReadOnlyInactive = [
        {username: "readOnlyUser", db: "admin", active: false},
        {username: "root", db: "admin", active: true}
    ];
    assert.soon(function() {
        return friendlyEqual(expectedReadOnlyInactive, readUserCache());
    });

    MongerRunner.stopMongerd(mongerd);
    awaitShell({checkExitSuccess: false});
})();
