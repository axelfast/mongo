// Test that a certificate with a valid CN, but invalid SAN
// does not permit connection, but provides a useful error.

(function() {
    'use strict';
    load('jstests/ssl/libs/ssl_helpers.js');

    // server-intermediate-ca was signed by ca.pem, not trusted-ca.pem
    const CA = 'jstests/libs/ca.pem';
    const SERVER = 'jstests/ssl/libs/localhost-cn-with-san.pem';

    const mongerd = MongerRunner.runMongerd({
        sslMode: 'requireSSL',
        sslPEMKeyFile: SERVER,
        sslCAFile: CA,
    });
    assert(mongerd);

    // Try with `tlsAllowInvalidHostnames` to look for the warning.
    clearRawMongerProgramOutput();
    const monger = runMongerProgram('monger',
                                  '--tls',
                                  '--tlsCAFile',
                                  CA,
                                  'localhost:' + mongerd.port,
                                  '--eval',
                                  ';',
                                  '--tlsAllowInvalidHostnames');
    assert.neq(monger, 0, "Shell connected when it should have failed");
    assert(rawMongerProgramOutput().includes(' would have matched, but was overridden by SAN'),
           'Expected detail warning not seen');

    // On OpenSSL only, start without `tlsAllowInvalidHostnames`
    // Windowds/Mac will bail out too early to show this message.
    if (determineSSLProvider() === 'openssl') {
        clearRawMongerProgramOutput();
        const monger = runMongerProgram(
            'monger', '--tls', '--tlsCAFile', CA, 'localhost:' + mongerd.port, '--eval', ';');
        assert.neq(monger, 0, "Shell connected when it should have failed");
        assert(rawMongerProgramOutput().includes(
                   'CN: localhost would have matched, but was overridden by SAN'),
               'Expected detail warning not seen');
    }

    MongerRunner.stopMongerd(mongerd);
})();
