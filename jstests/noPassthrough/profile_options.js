// @tags: [requires_profiling]

var baseName = "jstests_core_profile_options";

load('jstests/libs/command_line/test_parsed_options.js');

jsTest.log("Testing \"profile\" command line option with profiling off");
var expectedResult = {"parsed": {"operationProfiling": {"mode": "off"}}};
testGetCmdLineOptsMongerd({profile: "0"}, expectedResult);

jsTest.log("Testing \"profile\" command line option with profiling slow operations on");
var expectedResult = {"parsed": {"operationProfiling": {"mode": "slowOp"}}};
testGetCmdLineOptsMongerd({profile: "1"}, expectedResult);

jsTest.log("Testing \"profile\" command line option with profiling all on");
var expectedResult = {"parsed": {"operationProfiling": {"mode": "all"}}};
testGetCmdLineOptsMongerd({profile: "2"}, expectedResult);

jsTest.log("Testing \"operationProfiling.mode\" config file option");
expectedResult = {
    "parsed": {
        "config": "jstests/libs/config_files/set_profiling.json",
        "operationProfiling": {"mode": "all"}
    }
};
testGetCmdLineOptsMongerd({config: "jstests/libs/config_files/set_profiling.json"}, expectedResult);

print(baseName + " succeeded.");
