// Test passwords on private keys for SSL
// This tests that providing a proper password works and that providing no password or incorrect
// password fails.  It uses both mongerd and monger to run the tests, since the mongerd binary
// does not return error statuses to indicate an error.
// This test requires ssl support in monger-tools
// @tags: [requires_ssl_monger_tools]

load('jstests/ssl/libs/ssl_helpers.js');
requireSSLProvider('openssl', function() {
    var baseName = "jstests_ssl_ssl_cert_password";
    var dbpath = MongoRunner.dataPath + baseName;
    var external_scratch_dir = MongoRunner.dataPath + baseName + "/external/";
    resetDbpath(dbpath);
    mkdir(external_scratch_dir);

    // Password is correct
    var md = MongoRunner.runMongod({
        dbpath: dbpath,
        sslMode: "requireSSL",
        sslPEMKeyFile: "jstests/libs/password_protected.pem",
        sslPEMKeyPassword: "qwerty"
    });
    // MongoRunner.runMongod connects a Mongo shell, so if we get here, the test is successful.

    // Password incorrect; error logged is:
    //  error:06065064:digital envelope routines:EVP_DecryptFinal_ex:bad decrypt
    var exit_code = runMongoProgram("monger",
                                    "--port",
                                    md.port,
                                    "--ssl",
                                    "--sslAllowInvalidCertificates",
                                    "--sslCAFile",
                                    "jstests/libs/ca.pem",
                                    "--sslPEMKeyFile",
                                    "jstests/libs/password_protected.pem",
                                    "--sslPEMKeyPassword",
                                    "barf");

    // 1 is the exit code for failure
    assert(exit_code == 1);

    // Test that mongerdump and mongerrestore support ssl
    c = md.getDB("dumprestore_ssl").getCollection("foo");
    assert.eq(0, c.count(), "dumprestore_ssl.foo collection is not initially empty");
    c.save({a: 22});
    assert.eq(1, c.count(), "failed to insert document into dumprestore_ssl.foo collection");

    exit_code = MongoRunner.runMongoTool("mongerdump", {
        out: external_scratch_dir,
        port: md.port,
        ssl: "",
        sslPEMKeyFile: "jstests/libs/password_protected.pem",
        sslCAFile: "jstests/libs/ca.pem",
        sslPEMKeyPassword: "qwerty",
    });

    assert.eq(exit_code, 0, "Failed to start mongerdump with ssl");

    c.drop();
    assert.eq(0, c.count(), "dumprestore_ssl.foo collection is not empty after drop");

    exit_code = MongoRunner.runMongoTool("mongerrestore", {
        dir: external_scratch_dir,
        port: md.port,
        ssl: "",
        sslCAFile: "jstests/libs/ca.pem",
        sslPEMKeyFile: "jstests/libs/password_protected.pem",
        sslPEMKeyPassword: "qwerty",
    });

    assert.eq(exit_code, 0, "Failed to start mongerrestore with ssl");

    assert.soon("c.findOne()",
                "no data after sleep.  Expected a document after calling mongerrestore");
    assert.eq(
        1,
        c.count(),
        "did not find expected document in dumprestore_ssl.foo collection after mongerrestore");
    assert.eq(22, c.findOne().a, "did not find correct value in document after mongerrestore");

    // Test that mongerimport and mongerexport support ssl
    var exportimport_ssl_dbname = "exportimport_ssl";
    c = md.getDB(exportimport_ssl_dbname).getCollection("foo");
    assert.eq(0, c.count(), "exportimport_ssl.foo collection is not initially empty");
    c.save({a: 22});
    assert.eq(1, c.count(), "failed to insert document into exportimport_ssl.foo collection");

    var exportimport_file = "data.json";

    exit_code = MongoRunner.runMongoTool("mongerexport", {
        out: external_scratch_dir + exportimport_file,
        db: exportimport_ssl_dbname,
        collection: "foo",
        port: md.port,
        ssl: "",
        sslCAFile: "jstests/libs/ca.pem",
        sslPEMKeyFile: "jstests/libs/password_protected.pem",
        sslPEMKeyPassword: "qwerty",
    });

    assert.eq(exit_code, 0, "Failed to start mongerexport with ssl");

    c.drop();
    assert.eq(0, c.count(), "afterdrop", "-d", exportimport_ssl_dbname, "-c", "foo");

    exit_code = MongoRunner.runMongoTool("mongerimport", {
        file: external_scratch_dir + exportimport_file,
        db: exportimport_ssl_dbname,
        collection: "foo",
        port: md.port,
        ssl: "",
        sslCAFile: "jstests/libs/ca.pem",
        sslPEMKeyFile: "jstests/libs/password_protected.pem",
        sslPEMKeyPassword: "qwerty",
    });

    assert.eq(exit_code, 0, "Failed to start mongerimport with ssl");

    assert.soon("c.findOne()",
                "no data after sleep.  Expected a document after calling mongerimport");
    assert.eq(1,
              c.count(),
              "did not find expected document in dumprestore_ssl.foo collection after mongerimport");
    assert.eq(22, c.findOne().a, "did not find correct value in document after mongerimport");

    // Test that mongerfiles supports ssl
    var mongerfiles_ssl_dbname = "mongerfiles_ssl";
    mongerfiles_db = md.getDB(mongerfiles_ssl_dbname);

    source_filename = 'jstests/ssl/ssl_cert_password.js';
    filename = 'ssl_cert_password.js';

    exit_code = MongoRunner.runMongoTool("mongerfiles",
                                         {
                                           db: mongerfiles_ssl_dbname,
                                           port: md.port,
                                           ssl: "",
                                           sslCAFile: "jstests/libs/ca.pem",
                                           sslPEMKeyFile: "jstests/libs/password_protected.pem",
                                           sslPEMKeyPassword: "qwerty",
                                         },
                                         "put",
                                         source_filename);

    assert.eq(exit_code, 0, "Failed to start mongerfiles with ssl");

    md5 = md5sumFile(source_filename);
    file_obj = mongerfiles_db.fs.files.findOne();
    assert(file_obj, "failed to find file object in mongerfiles_ssl db using gridfs");
    md5_computed_res = mongerfiles_db.runCommand({filemd5: file_obj._id});
    assert.commandWorked(md5_computed_res);
    md5_computed = md5_computed_res.md5;
    assert(md5_computed);
    assert.eq(md5, md5_computed, "md5 computed incorrectly by server");

    exit_code = MongoRunner.runMongoTool("mongerfiles",
                                         {
                                           db: mongerfiles_ssl_dbname,
                                           local: external_scratch_dir + filename,
                                           port: md.port,
                                           ssl: "",
                                           sslCAFile: "jstests/libs/ca.pem",
                                           sslPEMKeyFile: "jstests/libs/password_protected.pem",
                                           sslPEMKeyPassword: "qwerty",
                                         },
                                         "get",
                                         source_filename);

    assert.eq(exit_code, 0, "Failed to start mongerfiles with ssl");

    md5 = md5sumFile(external_scratch_dir + filename);
    assert.eq(md5, md5_computed, "hash of stored file does not match the expected value");

    if (!_isWindows()) {
        // Stop the server
        var exitCode = MongoRunner.stopMongod(md);
        assert(exitCode == 0);
    } else {
        MongoRunner.stopMongod(md);
    }
});
