// Test that the ssl=true/false option is honored in shell URIs.

(function() {
    "use strict";

    var shouldSucceed = function(uri) {
        var conn = new Monger(uri);
        var res = conn.getDB('admin').runCommand({"ismaster": 1});
        assert(res.ok);
    };

    var shouldFail = function(uri) {
        assert.throws(function(uri) {
            var conn = new Monger(uri);
        }, [uri], "network error while attempting to run command");
    };

    // Start up a mongerd with ssl required.
    var sslMonger = MongerRunner.runMongerd({
        sslMode: "requireSSL",
        sslPEMKeyFile: "jstests/libs/server.pem",
        sslCAFile: "jstests/libs/ca.pem",
    });

    var sslURI = "mongerdb://localhost:" + sslMonger.port + "/admin";

    // When talking to a server with SSL, connecting with ssl=false fails.
    shouldSucceed(sslURI);
    shouldSucceed(sslURI + "?ssl=true");
    shouldFail(sslURI + "?ssl=false");

    var connectWithURI = function(uri) {
        return runMongerProgram('./monger',
                               '--ssl',
                               '--sslAllowInvalidCertificates',
                               '--sslCAFile',
                               'jstests/libs/ca.pem',
                               '--sslPEMKeyFile',
                               'jstests/libs/client.pem',
                               uri,
                               '--eval',
                               'db.runCommand({ismaster: 1})');
    };

    var shouldConnect = function(uri) {
        assert.eq(connectWithURI(uri), 0, "should have been able to connect with " + uri);
    };

    var shouldNotConnect = function(uri) {
        assert.eq(connectWithURI(uri), 1, "should not have been able to connect with " + uri);
    };

    // When talking to a server with SSL, connecting with ssl=false on the command line fails.
    shouldConnect(sslURI);
    shouldNotConnect(sslURI + "?ssl=false");
    shouldConnect(sslURI + "?ssl=true");

    // Connecting with ssl=true without --ssl will not work
    var res =
        runMongerProgram('./monger', sslURI + "?ssl=true", '--eval', 'db.runCommand({ismaster: 1})');
    assert.eq(res, 1, "should not have been able to connect without --ssl");

    // Clean up
    MongerRunner.stopMongerd(sslMonger);
}());
