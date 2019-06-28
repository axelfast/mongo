// Ensure that the shell may connect to servers running supporting restricted subsets of TLS
// protocols.

(function() {
    'use strict';

    var SERVER_CERT = "jstests/libs/server.pem";
    var CLIENT_CERT = "jstests/libs/client.pem";
    var CA_CERT = "jstests/libs/ca.pem";

    function runTestWithoutSubset(subset) {
        const disabledProtocols = subset.join(",");
        const conn = MongerRunner.runMongerd({
            sslMode: 'allowSSL',
            sslPEMKeyFile: SERVER_CERT,
            sslDisabledProtocols: disabledProtocols
        });

        const exitStatus = runMongerProgram('monger',
                                           '--ssl',
                                           '--sslAllowInvalidHostnames',
                                           '--sslPEMKeyFile',
                                           CLIENT_CERT,
                                           '--sslCAFile',
                                           CA_CERT,
                                           '--port',
                                           conn.port,
                                           '--eval',
                                           'quit()');

        assert.eq(0, exitStatus, "");

        MongerRunner.stopMongerd(conn);
    }

    runTestWithoutSubset(["TLS1_0"]);
    runTestWithoutSubset(["TLS1_2"]);
    runTestWithoutSubset(["TLS1_0", "TLS1_1"]);

})();
