// This test inserts multiple records into a collection creating a btree spanning multiple pages,
// restarts the server and scans the collection. This would trigger application thread to read from
// the disk as the startup would only partially load the collection data into the cache. In doing so
// check that the WiredTiger storage statistics are present in the slowop log message and in the
// system.profile collection for the profiled read operation.
//
// @tags: [requires_profiling]

(function() {
    'use strict';

    load("jstests/libs/profiler.js");  // For getLatestProfilerEntry.

    const readStatRegx = /storage:{ data: { bytesRead: ([0-9]+)/;

    let checkLogStats = function() {
        // Check if the log output contains the expected statistics.
        let mongerdLogs = rawMongerProgramOutput();
        let lines = mongerdLogs.split('\n');
        let match;
        let logLineCount = 0;
        for (let line of lines) {
            if ((match = readStatRegx.exec(line)) !== null) {
                jsTestLog(line);
                logLineCount++;
            }
        }
        assert.gte(logLineCount, 1);
    };

    let checkSystemProfileStats = function(profileObj, statName) {
        // Check that the profiled operation contains the expected statistics.
        assert(profileObj.hasOwnProperty("storage"), tojson(profileObj));
        assert(profileObj.storage.hasOwnProperty("data"), tojson(profileObj));
        assert(profileObj.storage.data.hasOwnProperty(statName), tojson(profileObj));
    };

    // This test can only be run if the storageEngine is wiredTiger
    if (jsTest.options().storageEngine && (jsTest.options().storageEngine !== "wiredTiger")) {
        jsTestLog("Skipping test because storageEngine is not wiredTiger");
    } else {
        let name = "wt_op_stat";

        jsTestLog("run mongerd");
        let conn = MongerRunner.runMongerd();
        assert.neq(null, conn, "mongerd was unable to start up");
        let testDB = conn.getDB(name);

        // Insert 200 documents of size 1K each, spanning multiple pages in the btree.
        let value = 'a'.repeat(1024);

        jsTestLog("insert data");
        for (let i = 0; i < 200; i++) {
            assert.writeOK(testDB.foo.insert({x: value}));
        }

        let connport = conn.port;
        MongerRunner.stopMongerd(conn);

        // Restart the server
        conn = MongerRunner.runMongerd({
            restart: true,
            port: connport,
            slowms: "0",
        });

        clearRawMongerProgramOutput();

        // Scan the collection and check the bytes read statistic in the slowop log and
        // system.profile.
        testDB = conn.getDB(name);
        testDB.setProfilingLevel(2);
        jsTestLog("read data");
        let cur = testDB.foo.find();
        while (cur.hasNext()) {
            cur.next();
        }

        // Look for the storage statistics in the profiled output of the find command.
        let profileObj = getLatestProfilerEntry(testDB, {op: "query", ns: "wt_op_stat.foo"});
        checkSystemProfileStats(profileObj, "bytesRead");

        // Stopping the mongerd waits until all of its logs have been read by the monger shell.
        MongerRunner.stopMongerd(conn);
        checkLogStats();

        jsTestLog("Success!");
    }
})();
