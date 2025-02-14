// Test X509 auth when --sslAllowInvalidCertificates is enabled

(function() {
    'use strict';

    const CLIENT_NAME = "CN=client,OU=KernelUser,O=MongerDB,L=New York City,ST=New York,C=US";
    const CLIENT_CERT = 'jstests/libs/client.pem';
    const SERVER_CERT = 'jstests/libs/server.pem';
    const CA_CERT = 'jstests/libs/ca.pem';
    const SELF_SIGNED_CERT = 'jstests/libs/client-self-signed.pem';

    function testClient(conn, cert, name, shouldSucceed) {
        let auth = {mechanism: 'MONGODB-X509'};
        if (name !== null) {
            auth.user = name;
        }
        const script = 'assert(db.getSiblingDB(\'$external\').auth(' + tojson(auth) + '));';
        clearRawMongerProgramOutput();
        const exitCode = runMongerProgram('monger',
                                         '--ssl',
                                         '--sslAllowInvalidHostnames',
                                         '--sslPEMKeyFile',
                                         cert,
                                         '--sslCAFile',
                                         CA_CERT,
                                         '--port',
                                         conn.port,
                                         '--eval',
                                         script);

        assert.eq(shouldSucceed, exitCode === 0, "exitCode = " + tojson(exitCode));
        assert.eq(
            !shouldSucceed,
            rawMongerProgramOutput().includes('No verified subject name available from client'));
    }

    function runTest(conn) {
        const admin = conn.getDB('admin');
        admin.createUser({user: "admin", pwd: "admin", roles: ["root"]});
        admin.auth('admin', 'admin');

        const external = conn.getDB('$external');
        external.createUser({user: CLIENT_NAME, roles: [{'role': 'readWrite', 'db': 'test'}]});

        testClient(conn, CLIENT_CERT, CLIENT_NAME, true);
        testClient(conn, SELF_SIGNED_CERT, CLIENT_NAME, false);
        testClient(conn, CLIENT_CERT, null, true);
        testClient(conn, SELF_SIGNED_CERT, null, false);
    }

    // Standalone.
    const mongerd = MongerRunner.runMongerd({
        auth: '',
        sslMode: 'requireSSL',
        sslPEMKeyFile: SERVER_CERT,
        sslCAFile: CA_CERT,
        sslAllowInvalidCertificates: '',
    });
    runTest(mongerd);
    MongerRunner.stopMongerd(mongerd);
})();
