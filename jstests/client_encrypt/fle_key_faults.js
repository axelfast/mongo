/**
 * Verify the KMS support handles a buggy Key Store
 */

load("jstests/client_encrypt/lib/mock_kms.js");
load('jstests/ssl/libs/ssl_helpers.js');

(function() {
    "use strict";

    const mock_kms = new MockKMSServer();
    mock_kms.start();

    const x509_options = {sslMode: "requireSSL", sslPEMKeyFile: SERVER_CERT, sslCAFile: CA_CERT};

    const conn = MongerRunner.runMongerd(x509_options);
    const test = conn.getDB("test");
    const collection = test.coll;

    const awsKMS = {
        accessKeyId: "access",
        secretAccessKey: "secret",
        url: mock_kms.getURL(),
    };

    var localKMS = {
        key: BinData(
            0,
            "/i8ytmWQuCe1zt3bIuVa4taPGKhqasVp0/0yI4Iy0ixQPNmeDF1J5qPUbBYoueVUJHMqj350eRTwztAWXuBdSQ=="),
    };

    const clientSideFLEOptions = {
        kmsProviders: {
            aws: awsKMS,
            local: localKMS,
        },
        keyVaultNamespace: "test.coll",
        schemaMap: {}
    };

    function testFault(kmsType, func) {
        collection.drop();

        const shell = Monger(conn.host, clientSideFLEOptions);
        const keyVault = shell.getKeyVault();

        assert.writeOK(
            keyVault.createKey(kmsType, "arn:aws:kms:us-east-1:fake:fake:fake", ['mongerKey']));
        const keyId = keyVault.getKeyByAltName("mongerKey").toArray()[0]._id;

        func(keyId, shell);
    }

    function testFaults(func) {
        const kmsTypes = ["aws", "local"];

        for (const kmsType of kmsTypes) {
            testFault(kmsType, func);
        }
    }

    // Negative - drop the key vault collection
    testFaults((keyId, shell) => {
        collection.drop();

        const str = "monger";
        assert.throws(() => {
            const encStr = shell.getClientEncryption().encrypt(keyId, str);
        });
    });

    // Negative - delete the keys
    testFaults((keyId, shell) => {
        collection.deleteMany({});

        const str = "monger";
        assert.throws(() => {
            const encStr = shell.getClientEncryption().encrypt(keyId, str);
        });
    });

    // Negative - corrupt the master key with an unkown provider
    testFaults((keyId, shell) => {
        collection.updateMany({}, {$set: {"masterKey.provider": "fake"}});

        const str = "monger";
        assert.throws(() => {
            const encStr = shell.getClientEncryption().encrypt(keyId, str);
        });
    });

    MongerRunner.stopMongerd(conn);
    mock_kms.stop();
}());