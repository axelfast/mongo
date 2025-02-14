/*
 * This file tests an encrypted shell started using command line parameters.
 *
 */
load('jstests/ssl/libs/ssl_helpers.js');

(function() {

    const x509_options = {sslMode: "requireSSL", sslPEMKeyFile: SERVER_CERT, sslCAFile: CA_CERT};
    const conn = MongerRunner.runMongerd(x509_options);

    const shellOpts = [
        "monger",
        "--host",
        conn.host,
        "--port",
        conn.port,
        "--tls",
        "--sslPEMKeyFile",
        CLIENT_CERT,
        "--sslCAFile",
        CA_CERT,
        "--tlsAllowInvalidHostnames",
        "--awsAccessKeyId",
        "access",
        "--awsSecretAccessKey",
        "secret",
        "--keyVaultNamespace",
        "test.coll",
        "--kmsURL",
        "https://localhost:8000",
    ];

    const testFiles = [
        "jstests/client_encrypt/lib/fle_command_line_explicit_encryption.js",
    ];

    for (const file of testFiles) {
        runMongerProgram(...shellOpts, file);
    }
}());