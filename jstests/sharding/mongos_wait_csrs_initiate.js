// Tests that mongers will wait for CSRS replica set to initiate.

load("jstests/libs/feature_compatibility_version.js");

var configRS = new ReplSetTest({name: "configRS", nodes: 1, useHostName: true});
configRS.startSet({configsvr: '', journal: "", storageEngine: 'wiredTiger'});
var replConfig = configRS.getReplSetConfig();
replConfig.configsvr = true;
var mongers = MongerRunner.runMongers({configdb: configRS.getURL(), waitForConnect: false});

assert.throws(function() {
    new Monger(mongers.host);
});

jsTestLog("Initiating CSRS");
configRS.initiate(replConfig);

// Ensure the featureCompatibilityVersion is lastStableFCV so that the mongers can connect if it is
// binary version last-stable.
assert.commandWorked(
    configRS.getPrimary().adminCommand({setFeatureCompatibilityVersion: lastStableFCV}));

jsTestLog("getting mongers");
var e;
assert.soon(
    function() {
        try {
            mongers2 = new Monger(mongers.host);
            return true;
        } catch (ex) {
            e = ex;
            return false;
        }
    },
    function() {
        return "mongers " + mongers.host +
            " did not begin accepting connections in time; final exception: " + tojson(e);
    });

jsTestLog("got mongers");
assert.commandWorked(mongers2.getDB('admin').runCommand('serverStatus'));
configRS.stopSet();
MongerRunner.stopMongers(mongers);