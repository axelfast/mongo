// Test for startuo warning when X509 auth and sslAllowInvalidCertificates are enabled

(function() {
    'use strict';

    function runTest(checkMongers, opts, expectWarningCertifcates, expectWarningHostnames) {
        clearRawMongerProgramOutput();
        let monger;

        if (checkMongers) {
            monger = MongerRunner.runMongers(Object.assign({
                configdb: "fakeRS/localhost:27017",
                waitForConnect: false,
            },
                                                        opts));
        } else {
            monger = MongerRunner.runMongerd(Object.assign({
                auth: '',
                sslMode: 'preferSSL',
                sslPEMKeyFile: 'jstests/libs/server.pem',
                sslCAFile: 'jstests/libs/ca.pem',
                waitForConnect: false,
            },
                                                        opts));
        }

        assert.soon(function() {
            const output = rawMongerProgramOutput();
            return (expectWarningCertifcates ==
                        output.includes('WARNING: While invalid X509 certificates may be used') &&
                    expectWarningHostnames ==
                        output.includes(
                            'WARNING: This server will not perform X.509 hostname validation'));
        });

        stopMongerProgramByPid(monger.pid);
    }

    function runTests(checkMongers) {
        // Don't expect a warning for certificates and hostnames when we're not using both options
        // together.
        runTest(checkMongers, {}, false, false);

        // Do expect a warning for certificates when we're combining options.
        runTest(checkMongers, {sslAllowInvalidCertificates: ''}, true, false);

        // Do expect a warning for hostnames.
        runTest(checkMongers, {sslAllowInvalidHostnames: ''}, false, true);

        // Do expect a warning for certificates and hostnames.
        runTest(checkMongers,
                {sslAllowInvalidCertificates: '', sslAllowInvalidHostnames: ''},
                true,
                true);
    }

    // Run tests on mongers
    runTests(true);

    // Run tests on mongerd
    runTests(false);

})();
