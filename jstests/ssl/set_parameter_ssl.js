// Test changing the --sslMode and --clusterAuthMode parameters using setParameter

var SERVER_CERT = "jstests/libs/server.pem";
var CA_CERT = "jstests/libs/ca.pem";

class TransportMode {
    constructor(sslName, tlsName) {
        this.sslName = sslName;
        this.tlsName = tlsName;
    }

    get sslMode() {
        return this.sslName;
    }

    get tlsMode() {
        return this.tlsName;
    }
}

const invalid = new TransportMode("invalid", "invalid");
const disabled = new TransportMode("disabled", "disabled");
const allowed = new TransportMode("allowSSL", "allowTLS");
const prefered = new TransportMode("preferSSL", "preferTLS");
const required = new TransportMode("requireSSL", "requireTLS");

function testTransportTransition(scheme, oldMode, newMode, shouldSucceed) {
    var conn =
        MongerRunner.runMongerd({sslMode: oldMode, sslPEMKeyFile: SERVER_CERT, sslCAFile: CA_CERT});

    var adminDB = conn.getDB("admin");
    adminDB.createUser({user: "root", pwd: "pwd", roles: ['root']});
    adminDB.auth("root", "pwd");
    var res = adminDB.runCommand({"setParameter": 1, [scheme]: newMode[scheme]});

    assert(res["ok"] == shouldSucceed, tojson(res));
    if (!shouldSucceed) {
        MongerRunner.stopMongerd(conn);
        return;
    }

    if (newMode != "requireSSL") {
        MongerRunner.stopMongerd(conn);
        return;
    }

    let uri = `mongerdb://localhost:${conn.port}/admin`;
    let exitCode = runMongerProgram("monger", uri, "--eval", "assert.commandWorked(db.isMaster())");
    assert.neq(exitCode, 0, "Was able to connect without SSL when SSLMode was requireSSL");
    MongerRunner.stopMongerd(conn);
}

function testAuthModeTransition(oldMode, newMode, sslMode, shouldSucceed) {
    var conn = MongerRunner.runMongerd({
        sslMode: sslMode,
        sslPEMKeyFile: SERVER_CERT,
        sslCAFile: CA_CERT,
        clusterAuthMode: oldMode
    });

    var adminDB = conn.getDB("admin");
    adminDB.createUser({user: "root", pwd: "pwd", roles: ['root']});
    adminDB.auth("root", "pwd");
    var res = adminDB.runCommand({"setParameter": 1, "clusterAuthMode": newMode});

    assert(res["ok"] == shouldSucceed, tojson(res));
    MongerRunner.stopMongerd(conn);
}

function testTransportTransitions(scheme) {
    testTransportTransition(scheme, "allowSSL", invalid, false);
    testTransportTransition(scheme, "allowSSL", disabled, false);
    testTransportTransition(scheme, "allowSSL", allowed, false);
    testTransportTransition(scheme, "allowSSL", prefered, true);
    testTransportTransition(scheme, "allowSSL", required, false);
    testTransportTransition(scheme, "preferSSL", invalid, false);
    testTransportTransition(scheme, "preferSSL", disabled, false);
    testTransportTransition(scheme, "preferSSL", allowed, false);
    testTransportTransition(scheme, "preferSSL", prefered, false);
    testTransportTransition(scheme, "preferSSL", required, true);
    testTransportTransition(scheme, "requireSSL", invalid, false);
    testTransportTransition(scheme, "requireSSL", disabled, false);
    testTransportTransition(scheme, "requireSSL", allowed, false);
    testTransportTransition(scheme, "requireSSL", prefered, false);
    testTransportTransition(scheme, "requireSSL", required, false);
}

testTransportTransitions("sslMode");
testTransportTransitions("tlsMode");

testAuthModeTransition("sendKeyFile", "invalid", "requireSSL", false);
testAuthModeTransition("sendKeyFile", "keyFile", "requireSSL", false);
testAuthModeTransition("sendKeyFile", "sendKeyFile", "requireSSL", false);
testAuthModeTransition("sendKeyFile", "sendX509", "requireSSL", true);
testAuthModeTransition("sendKeyFile", "x509", "requireSSL", false);
testAuthModeTransition("sendX509", "invalid", "requireSSL", false);
testAuthModeTransition("sendX509", "keyFile", "requireSSL", false);
testAuthModeTransition("sendX509", "sendKeyFile", "requireSSL", false);
testAuthModeTransition("sendX509", "sendX509", "requireSSL", false);
testAuthModeTransition("sendX509", "x509", "requireSSL", true);
testAuthModeTransition("x509", "invalid", "requireSSL", false);
testAuthModeTransition("x509", "keyFile", "requireSSL", false);
testAuthModeTransition("x509", "sendKeyFile", "requireSSL", false);
testAuthModeTransition("x509", "sendX509", "requireSSL", false);
testAuthModeTransition("x509", "x509", "requireSSL", false);

testAuthModeTransition("sendKeyFile", "invalid", "allowSSL", false);
testAuthModeTransition("sendKeyFile", "keyFile", "allowSSL", false);
testAuthModeTransition("sendKeyFile", "sendKeyFile", "allowSSL", false);
testAuthModeTransition("sendKeyFile", "sendX509", "allowSSL", false);
testAuthModeTransition("sendKeyFile", "x509", "allowSSL", false);
