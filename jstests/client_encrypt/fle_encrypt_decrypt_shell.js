/**
 * Check the functionality of encrypt and decrypt functions in KeyStore.js
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

    let localKMS = {
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

    const kmsTypes = ["aws", "local"];

    const randomAlgorithm = "AEAD_AES_256_CBC_HMAC_SHA_512-Random";
    const deterministicAlgorithm = "AEAD_AES_256_CBC_HMAC_SHA_512-Deterministic";
    const encryptionAlgorithms = [randomAlgorithm, deterministicAlgorithm];

    const passTestCases = [
        "monger",
        NumberLong(13),
        NumberInt(23),
        UUID(),
        ISODate(),
        new Date('December 17, 1995 03:24:00'),
        BinData(2, '1234'),
        new Timestamp(1, 2),
        new ObjectId(),
        new DBPointer("monger", new ObjectId()),
        /test/
    ];

    const failDeterministic = [
        true,
        false,
        12,
        NumberDecimal(0.1234),
        ["this is an array"],
        {"value": "monger"},
        Code("function() { return true; }")
    ];

    const failTestCases = [null, undefined, MinKey(), MaxKey(), DBRef("test", "test", "test")];

    const shell = Monger(conn.host, clientSideFLEOptions);
    const keyVault = shell.getKeyVault();

    // Testing for every combination of (kmsType, algorithm, javascriptVariable)
    for (const kmsType of kmsTypes) {
        for (const encryptionAlgorithm of encryptionAlgorithms) {
            collection.drop();

            assert.writeOK(
                keyVault.createKey(kmsType, "arn:aws:kms:us-east-1:fake:fake:fake", ['mongerKey']));
            const keyId = keyVault.getKeyByAltName("mongerKey").toArray()[0]._id;

            let pass;
            let fail;
            if (encryptionAlgorithm === randomAlgorithm) {
                pass = [...passTestCases, ...failDeterministic];
                fail = failTestCases;
            } else if (encryptionAlgorithm === deterministicAlgorithm) {
                pass = passTestCases;
                fail = [...failTestCases, ...failDeterministic];
            }

            const clientEncrypt = shell.getClientEncryption();
            for (const passTestCase of pass) {
                const encPassTestCase =
                    clientEncrypt.encrypt(keyId, passTestCase, encryptionAlgorithm);
                assert.eq(passTestCase, clientEncrypt.decrypt(encPassTestCase));

                if (encryptionAlgorithm === deterministicAlgorithm) {
                    assert.eq(encPassTestCase,
                              clientEncrypt.encrypt(keyId, passTestCase, encryptionAlgorithm));
                }
            }

            for (const failTestCase of fail) {
                assert.throws(() =>
                                  clientEncrypt.encrypt(keyId, failTestCase, encryptionAlgorithm));
            }
        }
    }

    MongerRunner.stopMongerd(conn);
    mock_kms.stop();
}());