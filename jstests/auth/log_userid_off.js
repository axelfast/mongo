/**
 * Tests that logged users will not show up in the log.
 *
 * @param monger {Monger} connection object.
 * @tags: [requires_sharding]
 */
var doTest = function(monger, callSetParam) {
    var TEST_USER = 'foo';
    var TEST_PWD = 'bar';
    var testDB = monger.getDB('test');

    testDB.createUser({user: TEST_USER, pwd: TEST_PWD, roles: jsTest.basicUserRoles});
    testDB.auth(TEST_USER, TEST_PWD);

    testDB.runCommand({dbStats: 1});

    var log = testDB.adminCommand({getLog: 'global'});
    log.log.forEach(function(line) {
        assert.eq(-1, line.indexOf('user: foo@'), 'user logged: ' + line);
    });

    // logUserIds should not be settable
    var res = testDB.runCommand({setParameter: 1, logUserIds: 1});
    assert(!res.ok);

    testDB.runCommand({dbStats: 1});

    log = testDB.adminCommand({getLog: 'global'});
    log.log.forEach(function(line) {
        assert.eq(-1, line.indexOf('user: foo@'), 'user logged: ' + line);
    });
};

var monger = MongerRunner.runMongerd({verbose: 5});
doTest(monger);
MongerRunner.stopMongerd(monger);

var st = new ShardingTest({shards: 1, verbose: 5});
doTest(st.s);
st.stop();
