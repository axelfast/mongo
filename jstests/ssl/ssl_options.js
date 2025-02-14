// Test redaction of passwords in command line SSL option parsing.

load('jstests/ssl/libs/ssl_helpers.js');
requireSSLProvider('openssl', function() {
    'use strict';

    const baseName = "jstests_ssl_ssl_options";

    jsTest.log("Testing censorship of ssl options");

    const mongerdConfig = {
        sslPEMKeyFile: "jstests/libs/password_protected.pem",
        sslMode: "requireSSL",
        sslPEMKeyPassword: "qwerty",
        sslClusterPassword: "qwerty",
        sslCAFile: "jstests/libs/ca.pem"
    };
    const mongerdSource = MongerRunner.runMongerd(mongerdConfig);

    const getCmdLineOptsResult = mongerdSource.adminCommand("getCmdLineOpts");

    let i;
    let isPassword = false;
    for (i = 0; i < getCmdLineOptsResult.argv.length; i++) {
        if (isPassword) {
            assert.eq(getCmdLineOptsResult.argv[i],
                      "<password>",
                      "Password not properly censored: " + tojson(getCmdLineOptsResult));
            isPassword = false;
            continue;
        }

        if (getCmdLineOptsResult.argv[i] === "--sslPEMKeyPassword" ||
            getCmdLineOptsResult.argv[i] === "--sslClusterPassword") {
            isPassword = true;
        }
    }

    assert.eq(getCmdLineOptsResult.parsed.net.tls.certificateKeyFilePassword,
              "<password>",
              "Password not properly censored: " + tojson(getCmdLineOptsResult));
    assert.eq(getCmdLineOptsResult.parsed.net.tls.clusterPassword,
              "<password>",
              "Password not properly censored: " + tojson(getCmdLineOptsResult));

    MongerRunner.stopMongerd(mongerdSource);

    print(baseName + " succeeded.");
});
