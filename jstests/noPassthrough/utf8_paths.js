/**
 * Test that verifies mongerd can start using paths that contain UTF-8 characters that are not ASCII.
 */
(function() {
    'use strict';
    var db_name = "ελληνικά";
    var path = MongerRunner.dataPath + "Росси́я";

    mkdir(path);

    // Test MongerD
    let testMongerD = function() {
        let options = {
            dbpath: path,
            useLogFiles: true,
            pidfilepath: path + "/pidfile",
        };

        // directoryperdb is only supported with the wiredTiger storage engine
        if (!jsTest.options().storageEngine || jsTest.options().storageEngine === "wiredTiger") {
            options["directoryperdb"] = "";
        }

        let conn = MongerRunner.runMongerd(options);
        assert.neq(null, conn, 'mongerd was unable to start up');

        let coll = conn.getCollection(db_name + ".foo");
        assert.writeOK(coll.insert({_id: 1}));

        MongerRunner.stopMongerd(conn);
    };

    testMongerD();

    // Start a second time to test things like log rotation.
    testMongerD();
})();
