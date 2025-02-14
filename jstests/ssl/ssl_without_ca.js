var SERVER_CERT = "jstests/libs/server.pem";
var CLIENT_CERT = "jstests/libs/client.pem";
var CLIENT_USER = "C=US,ST=New York,L=New York City,O=MongerDB,OU=KernelUser,CN=client";

jsTest.log("Assert x509 auth is not allowed when a standalone mongerd is run without a CA file.");

// allowSSL instead of requireSSL so that the non-SSL connection succeeds.
var conn = MongerRunner.runMongerd({sslMode: 'allowSSL', sslPEMKeyFile: SERVER_CERT, auth: ''});

var external = conn.getDB('$external');
external.createUser({
    user: CLIENT_USER,
    roles: [
        {'role': 'userAdminAnyDatabase', 'db': 'admin'},
        {'role': 'readWriteAnyDatabase', 'db': 'admin'}
    ]
});

// Should not be able to authenticate with x509.
// Authenticate call will return 1 on success, 0 on error.
var exitStatus = runMongerProgram('monger',
                                 '--ssl',
                                 '--sslAllowInvalidCertificates',
                                 '--sslPEMKeyFile',
                                 CLIENT_CERT,
                                 '--port',
                                 conn.port,
                                 '--eval',
                                 ('quit(db.getSisterDB("$external").auth({' +
                                  'user: "' + CLIENT_USER + '" ,' +
                                  'mechanism: "MONGODB-X509"}));'));

assert.eq(exitStatus, 0, "authentication via MONGODB-X509 without CA succeeded");

MongerRunner.stopMongerd(conn);

jsTest.log("Assert mongerd doesn\'t start with CA file missing and clusterAuthMode=x509.");

var sslParams = {clusterAuthMode: 'x509', sslMode: 'requireSSL', sslPEMKeyFile: SERVER_CERT};
var conn = MongerRunner.runMongerd(sslParams);
assert.isnull(conn, "server started with x509 clusterAuthMode but no CA file");

jsTest.log("Assert mongers doesn\'t start with CA file missing and clusterAuthMode=x509.");

// TODO: Remove 'shardAsReplicaSet: false' when SERVER-32672 is fixed.
assert.throws(function() {
    new ShardingTest({
        shards: 1,
        mongers: 1,
        verbose: 2,
        other: {
            configOptions: sslParams,
            mongersOptions: sslParams,
            shardOptions: sslParams,
            shardAsReplicaSet: false
        }
    });
}, [], "mongers started with x509 clusterAuthMode but no CA file");
