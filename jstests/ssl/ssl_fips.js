// Test mongerd start with FIPS mode enabled
var port = allocatePort();
var md = MongerRunner.runMongerd({
    port: port,
    sslMode: "requireSSL",
    sslPEMKeyFile: "jstests/libs/server.pem",
    sslCAFile: "jstests/libs/ca.pem",
    sslFIPSMode: ""
});

var monger = runMongerProgram("monger",
                            "--port",
                            port,
                            "--ssl",
                            "--sslAllowInvalidCertificates",
                            "--sslPEMKeyFile",
                            "jstests/libs/client.pem",
                            "--sslFIPSMode",
                            "--eval",
                            ";");

// if monger shell didn't start/connect properly
if (monger != 0) {
    print("mongerd failed to start, checking for FIPS support");
    mongerOutput = rawMongerProgramOutput();
    assert(mongerOutput.match(/this version of mongerdb was not compiled with FIPS support/) ||
           mongerOutput.match(/FIPS modes is not enabled on the operating system/) ||
           mongerOutput.match(/FIPS_mode_set:fips mode not supported/));
} else {
    // verify that auth works, SERVER-18051
    md.getDB("admin").createUser({user: "root", pwd: "root", roles: ["root"]});
    assert(md.getDB("admin").auth("root", "root"), "auth failed");

    // kill mongerd
    MongerRunner.stopMongerd(md);
}
