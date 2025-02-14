// Ensure that TLS version alerts are correctly propagated

load('jstests/ssl/libs/ssl_helpers.js');

(function() {
    'use strict';

    const clientOptions = [
        "--ssl",
        "--sslPEMKeyFile",
        "jstests/libs/client.pem",
        "--sslCAFile",
        "jstests/libs/ca.pem",
        "--eval",
        ";"
    ];

    function runTest(serverDisabledProtos, clientDisabledProtos) {
        const implementation = determineSSLProvider();
        let expectedRegex;
        if (implementation === "openssl") {
            expectedRegex =
                /Error: couldn't connect to server .*:[0-9]*, connection attempt failed: SocketException: tlsv1 alert protocol version/;
        } else if (implementation === "windows") {
            expectedRegex =
                /Error: couldn't connect to server .*:[0-9]*, connection attempt failed: SocketException: The function requested is not supported/;
        } else if (implementation === "apple") {
            expectedRegex =
                /Error: couldn't connect to server .*:[0-9]*, connection attempt failed: SocketException: Secure.Transport: bad protocol version/;
        } else {
            throw Error("Unrecognized TLS implementation!");
        }

        var md = MongerRunner.runMongerd({
            sslMode: "requireSSL",
            sslCAFile: "jstests/libs/ca.pem",
            sslPEMKeyFile: "jstests/libs/server.pem",
            sslDisabledProtocols: serverDisabledProtos,
        });

        let shell;
        let mongerOutput;

        assert.soon(function() {
            clearRawMongerProgramOutput();
            shell = runMongerProgram("monger",
                                    "--port",
                                    md.port,
                                    ...clientOptions,
                                    "--sslDisabledProtocols",
                                    clientDisabledProtos);
            mongerOutput = rawMongerProgramOutput();
            return mongerOutput.match(expectedRegex);
        }, "Monger shell output was as follows:\n" + mongerOutput + "\n************");

        MongerRunner.stopMongerd(md);
    }

    // Client recieves and reports a protocol version alert if it advertises a protocol older than
    // the server's oldest supported protocol
    runTest("TLS1_0", "TLS1_1,TLS1_2");
}());
