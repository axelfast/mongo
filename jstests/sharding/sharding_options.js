// TODO SERVER-35447: Multiple users cannot be authenticated on one connection within a session.
TestData.disableImplicitSessions = true;

var baseName = "jstests_sharding_sharding_options";

load('jstests/libs/command_line/test_parsed_options.js');

// Move Paranoia
jsTest.log("Testing \"moveParanoia\" command line option");
var expectedResult = {"parsed": {"sharding": {"archiveMovedChunks": true}}};
testGetCmdLineOptsMongerd({moveParanoia: ""}, expectedResult);

jsTest.log("Testing \"noMoveParanoia\" command line option");
expectedResult = {
    "parsed": {"sharding": {"archiveMovedChunks": false}}
};
testGetCmdLineOptsMongerd({noMoveParanoia: ""}, expectedResult);

jsTest.log("Testing \"sharding.archiveMovedChunks\" config file option");
expectedResult = {
    "parsed": {
        "config": "jstests/libs/config_files/enable_paranoia.json",
        "sharding": {"archiveMovedChunks": true}
    }
};
testGetCmdLineOptsMongerd({config: "jstests/libs/config_files/enable_paranoia.json"},
                         expectedResult);

// Sharding Role
jsTest.log("Testing \"configsvr\" command line option");
var expectedResult = {
    "parsed":
        {"sharding": {"clusterRole": "configsvr"}, "storage": {"journal": {"enabled": true}}}
};
testGetCmdLineOptsMongerd({configsvr: "", journal: ""}, expectedResult);

jsTest.log("Testing \"shardsvr\" command line option");
expectedResult = {
    "parsed": {"sharding": {"clusterRole": "shardsvr"}}
};
testGetCmdLineOptsMongerd({shardsvr: ""}, expectedResult);

jsTest.log("Testing \"sharding.clusterRole\" config file option");
expectedResult = {
    "parsed": {
        "config": "jstests/libs/config_files/set_shardingrole.json",
        "sharding": {"clusterRole": "configsvr"}
    }
};
testGetCmdLineOptsMongerd({config: "jstests/libs/config_files/set_shardingrole.json"},
                         expectedResult);

// Test that we preserve switches explicitly set to false in config files.  See SERVER-13439.
jsTest.log("Testing explicitly disabled \"moveParanoia\" config file option");
expectedResult = {
    "parsed": {
        "config": "jstests/libs/config_files/disable_moveparanoia.ini",
        "sharding": {"archiveMovedChunks": false}
    }
};
testGetCmdLineOptsMongerd({config: "jstests/libs/config_files/disable_moveparanoia.ini"},
                         expectedResult);

jsTest.log("Testing explicitly disabled \"noMoveParanoia\" config file option");
expectedResult = {
    "parsed": {
        "config": "jstests/libs/config_files/disable_nomoveparanoia.ini",
        "sharding": {"archiveMovedChunks": true}
    }
};
testGetCmdLineOptsMongerd({config: "jstests/libs/config_files/disable_nomoveparanoia.ini"},
                         expectedResult);

print(baseName + " succeeded.");
