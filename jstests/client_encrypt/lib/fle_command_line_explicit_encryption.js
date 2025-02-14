/**
* Check the functionality of encrypt and decrypt functions in KeyVault.js. This test is run by
* jstests/fle/fle_command_line_encryption.js.
*/

load("jstests/client_encrypt/lib/mock_kms.js");

(function() {
    "use strict";

    const mock_kms = new MockKMSServer();
    mock_kms.start();

    const shell = Monger();
    const keyVault = shell.getKeyVault();

    const test = shell.getDB("test");
    const collection = test.coll;

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

    // Testing for every combination of (algorithm, javascriptVariable)
    for (const encryptionAlgorithm of encryptionAlgorithms) {
        collection.drop();

        assert.writeOK(
            keyVault.createKey("aws", "arn:aws:kms:us-east-1:fake:fake:fake", ['mongerKey']));
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

        for (const passTestCase of pass) {
            const encPassTestCase = shell.encrypt(keyId, passTestCase, encryptionAlgorithm);
            assert.eq(passTestCase, shell.decrypt(encPassTestCase));

            if (encryptionAlgorithm == deterministicAlgorithm) {
                assert.eq(encPassTestCase, shell.encrypt(keyId, passTestCase, encryptionAlgorithm));
            }
        }

        for (const failTestCase of fail) {
            assert.throws(shell.encrypt, [keyId, failTestCase, encryptionAlgorithm]);
        }
    }

    mock_kms.stop();
    print("Test completed with no errors.");
}());