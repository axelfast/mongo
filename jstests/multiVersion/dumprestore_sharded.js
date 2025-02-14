load('./jstests/multiVersion/libs/dumprestore_helpers.js');

// The base name to use for various things in the test, including the dbpath and the database name
var testBaseName = "jstests_tool_dumprestore_sharded";

// Paths to external directories to be used to store dump files
var dumpDir = MongerRunner.dataPath + testBaseName + "_dump_external/";
var testDbpath = MongerRunner.dataPath + testBaseName + "_dbpath_external/";

// Test dumping from a sharded cluster across versions
var shardedDumpTests = {
    'serverSourceVersion': ["latest", "last-stable"],
    'serverDestVersion': ["latest", "last-stable"],
    'mongerDumpVersion': ["latest", "last-stable"],
    'mongerRestoreVersion': ["latest", "last-stable"],
    'dumpDir': [dumpDir],
    'testDbpath': [testDbpath],
    'dumpType': ["mongers"],
    'restoreType': ["mongerd"],
    'storageEngine': [jsTest.options().storageEngine || "wiredTiger"]
};
runAllDumpRestoreTests(shardedDumpTests);

// Test restoring to a sharded cluster across versions
var shardedRestoreTests = {
    'serverSourceVersion': ["latest", "last-stable"],
    'serverDestVersion': ["latest", "last-stable"],
    'mongerDumpVersion': ["latest", "last-stable"],
    'mongerRestoreVersion': ["latest", "last-stable"],
    'dumpDir': [dumpDir],
    'testDbpath': [testDbpath],
    'dumpType': ["mongerd"],
    'restoreType': ["mongers"],
    'storageEngine': [jsTest.options().storageEngine || "wiredTiger"]
};
runAllDumpRestoreTests(shardedRestoreTests);
