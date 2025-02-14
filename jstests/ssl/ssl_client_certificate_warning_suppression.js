/**
 * Tests the startup-only setParameter value suppressNoTLSPeerCertificateWarning which suppresses
 * the log message "no SSL certificate provided by peer" when a client certificate is not provided.
 * This only works if weak validation is enabled.
 *
 * This test confirms that the log message is output when the setParameter is set to true,
 * and is not output when the setParameter is set to false.
 */

load('jstests/ssl/libs/ssl_helpers.js');

(function() {
    'use strict';

    function test(suppress) {
        const opts = {
            sslMode: 'requireSSL',
            sslPEMKeyFile: "jstests/libs/server.pem",
            sslCAFile: "jstests/libs/ca.pem",
            waitForConnect: false,
            sslAllowConnectionsWithoutCertificates: "",
            setParameter: {suppressNoTLSPeerCertificateWarning: suppress}
        };
        clearRawMongerProgramOutput();
        const mongerd = MongerRunner.runMongerd(opts);

        assert.soon(function() {
            return runMongerProgram('monger',
                                   '--ssl',
                                   '--sslAllowInvalidHostnames',
                                   '--sslCAFile',
                                   CA_CERT,
                                   '--port',
                                   mongerd.port,
                                   '--eval',
                                   'quit()') === 0;
        }, "monger did not initialize properly");

        // Keep checking the log file until client metadata is logged since the SSL warning is
        // logged before it.
        assert.soon(
            () => {
                const log = rawMongerProgramOutput();
                return log.search('client metadata') !== -1;
            },
            "logfile should contain 'client metadata'.\n" +
                "Log File Contents\n==============================\n" + rawMongerProgramOutput() +
                "\n==============================\n");

        // Now check for the message
        const log = rawMongerProgramOutput();
        assert.eq(suppress, log.search('no SSL certificate provided by peer') === -1);

        try {
            MongerRunner.stopMongerd(mongerd);
        } catch (e) {
            // Depending on timing, exitCode might be 0, 1, or -9.
            // All that matters is that it dies, resmoke will tell us if that failed.
            // So just let it go, the exit code never bothered us anyway.
        }
    }

    test(true);
    test(false);
})();
