// Make sure the psuedo-option --tlsOnNormalPorts is correctly canonicalized.

(function() {
    'use strict';

    const mongerd = MongoRunner.runMongod({
        tlsOnNormalPorts: '',
        tlsCertificateKeyFile: 'jstests/libs/server.pem',
    });
    assert(mongerd);
    assert.commandWorked(mongerd.getDB('admin').runCommand({isMaster: 1}));
    MongoRunner.stopMongod(mongerd);
})();
