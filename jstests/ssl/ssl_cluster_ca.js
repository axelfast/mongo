// Verify certificates and CAs between intra-cluster
// and client->server communication using different CAs.

(function() {
    "use strict";

    function testRS(opts, succeed) {
        const origSkipCheck = TestData.skipCheckDBHashes;
        const rsOpts = {
            // Use localhost so that SAN matches.
            useHostName: false,
            nodes: {node0: opts, node1: opts},
        };
        const rs = new ReplSetTest(rsOpts);
        rs.startSet();
        if (succeed) {
            rs.initiate();
            assert.commandWorked(rs.getPrimary().getDB('admin').runCommand({isMaster: 1}));
        } else {
            assert.throws(function() {
                rs.initiate();
            });
            TestData.skipCheckDBHashes = true;
        }
        rs.stopSet();
        TestData.skipCheckDBHashes = origSkipCheck;
    }

    // The name "trusted" in these certificates is misleading.
    // They're just a separate trust chain from the ones without the name.
    // ca.pem signed client.pem and server.pem
    // trusted-ca.pem signed trusted-client.pem and trusted-server.pem
    const valid_options = {
        tlsMode: 'requireTLS',
        // Servers present trusted-server.pem to clients and each other for inbound connections.
        // Peers validate trusted-server.pem using trusted-ca.pem when making those connections.
        tlsCertificateKeyFile: 'jstests/libs/trusted-server.pem',
        tlsCAFile: 'jstests/libs/trusted-ca.pem',
        // Servers making outbound connections to other servers present server.pem to their peers
        // which their peers validate using ca.pem.
        tlsClusterFile: 'jstests/libs/server.pem',
        tlsClusterCAFile: 'jstests/libs/ca.pem',
        // SERVER-36895: IP based hostname validation with SubjectAlternateName
        tlsAllowInvalidHostnames: '',
    };

    testRS(valid_options, true);

    const wrong_cluster_file =
        Object.assign({}, valid_options, {tlsClusterFile: valid_options.tlsCertificateKeyFile});
    testRS(wrong_cluster_file, false);

    const wrong_key_file =
        Object.assign({}, valid_options, {tlsCertificateKeyFile: valid_options.tlsClusterFile});
    testRS(wrong_key_file, false);

    const mongerd = MongerRunner.runMongerd(valid_options);
    assert(mongerd, "Failed starting standalone mongerd with alternate CA");

    function testConnect(cert, succeed) {
        const monger = runMongerProgram("monger",
                                      "--host",
                                      "localhost",
                                      "--port",
                                      mongerd.port,
                                      "--tls",
                                      "--tlsCAFile",
                                      valid_options.tlsCAFile,
                                      "--tlsCertificateKeyFile",
                                      cert,
                                      "--eval",
                                      ";");

        // runMongerProgram returns 0 on success
        assert.eq(monger === 0, succeed);
    }

    testConnect('jstests/libs/client.pem', true);
    testConnect('jstests/libs/trusted-client.pem', false);

    MongerRunner.stopMongerd(mongerd);
}());
