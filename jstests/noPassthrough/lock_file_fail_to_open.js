// Tests that MongerD fails to start with the correct error message if mongerd.lock exists in the
// dbpath.
(function() {
    "use strict";

    var baseName = "jstests_lock_file_fail_to_open";

    var dbPath = MongerRunner.dataPath + baseName + "/";

    // Start a MongerD just to get a lockfile in place.
    var monger1 = MongerRunner.runMongerd({dbpath: dbPath, waitForConnect: true});

    clearRawMongerProgramOutput();
    // Start another one which should fail to start as there is already a lockfile in its
    // dbpath.
    var monger2 = null;
    monger2 = MongerRunner.runMongerd({dbpath: dbPath, noCleanData: true});
    // We should have failed to start.
    assert(monger2 === null);

    var logContents = rawMongerProgramOutput();
    assert(logContents.indexOf("Unable to lock the lock file") > 0 ||
           // Windows error message is different.
           logContents.indexOf("Unable to create/open the lock file") > 0);

    MongerRunner.stopMongerd(monger1);
})();
