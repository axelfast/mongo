(function() {
    "use strict";

    var CA_CERT = "jstests/libs/ca.pem";
    var SERVER_CERT = "jstests/libs/server.pem";
    var CLIENT_CERT = "jstests/libs/client.pem";
    var BAD_SAN_CERT = "jstests/libs/badSAN.pem";

    var mongerd = MongerRunner.runMongerd({
        sslMode: "requireSSL",
        sslPEMKeyFile: SERVER_CERT,
        sslCAFile: CA_CERT,
        sslClusterFile: BAD_SAN_CERT
    });

    var monger = runMongerProgram("monger",
                                "--host",
                                "localhost",
                                "--port",
                                mongerd.port,
                                "--ssl",
                                "--sslCAFile",
                                CA_CERT,
                                "--sslPEMKeyFile",
                                CLIENT_CERT,
                                "--eval",
                                ";");

    // runMongerProgram returns 0 on success
    assert.eq(
        0,
        monger,
        "Connection attempt failed when an irrelevant sslClusterFile was provided to the server!");
    MongerRunner.stopMongerd(mongerd);
}());
