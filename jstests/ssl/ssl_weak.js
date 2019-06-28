// Test forcing certificate validation
// This tests that forcing certification validation will prohibit clients without certificates
// from connecting.

// Test that connecting with no client certificate and --sslAllowConnectionsWithoutCertificates
// (an alias for sslWeakCertificateValidation) connects successfully.
var md = MongerRunner.runMongerd({
    sslMode: "requireSSL",
    sslPEMKeyFile: "jstests/libs/server.pem",
    sslCAFile: "jstests/libs/ca.pem",
    sslAllowConnectionsWithoutCertificates: ""
});

var monger = runMongerProgram(
    "monger", "--port", md.port, "--ssl", "--sslAllowInvalidCertificates", "--eval", ";");

// 0 is the exit code for success
assert(monger == 0);

// Test that connecting with a valid client certificate connects successfully.
monger = runMongerProgram("monger",
                        "--port",
                        md.port,
                        "--ssl",
                        "--sslAllowInvalidCertificates",
                        "--sslPEMKeyFile",
                        "jstests/libs/client.pem",
                        "--eval",
                        ";");

// 0 is the exit code for success
assert(monger == 0);
MongerRunner.stopMongerd(md);
// Test that connecting with no client certificate and no --sslAllowConnectionsWithoutCertificates
// fails to connect.
var md2 = MongerRunner.runMongerd({
    sslMode: "requireSSL",
    sslPEMKeyFile: "jstests/libs/server.pem",
    sslCAFile: "jstests/libs/ca.pem"
});

monger = runMongerProgram(
    "monger", "--port", md2.port, "--ssl", "--sslAllowInvalidCertificates", "--eval", ";");

// 1 is the exit code for failure
assert(monger == 1);
MongerRunner.stopMongerd(md2);