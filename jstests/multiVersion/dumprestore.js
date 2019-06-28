// dumprestore.js

load('./jstests/multiVersion/libs/dumprestore_helpers.js');

// The base name to use for various things in the test, including the dbpath and the database name
var testBaseName = "jstests_tool_dumprestore";

// Paths to external directories to be used to store dump files
var dumpDir = MongerRunner.dataPath + testBaseName + "_dump_external/";
var testDbpath = MongerRunner.dataPath + testBaseName + "_dbpath_external/";

// Start with basic multiversion tests just running against a single mongerd
var singleNodeTests = {
    'serverSourceVersion': ["latest", "last-stable"],
    'serverDestVersion': ["latest", "last-stable"],
    'mongerDumpVersion': ["latest", "last-stable"],
    'mongerRestoreVersion': ["latest", "last-stable"],
    'dumpDir': [dumpDir],
    'testDbpath': [testDbpath],
    'dumpType': ["mongerd"],
    'restoreType': ["mongerd"],
    'storageEngine': [jsTest.options().storageEngine || "wiredTiger"]
};
runAllDumpRestoreTests(singleNodeTests);
