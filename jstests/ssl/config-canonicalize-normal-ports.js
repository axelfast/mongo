// Make sure the psuedo-option --tlsOnNormalPorts is correctly canonicalized.

(function() {
    'use strict';

    const mongerd = MongerRunner.runMongerd({
        tlsOnNormalPorts: '',
        tlsCertificateKeyFile: 'jstests/libs/server.pem',
    });
    assert(mongerd);
    assert.commandWorked(mongerd.getDB('admin').runCommand({isMaster: 1}));
    MongerRunner.stopMongerd(mongerd);
})();
