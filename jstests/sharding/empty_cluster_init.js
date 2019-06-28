//
// Tests initialization of an empty cluster with multiple mongerses.
// Starts a bunch of mongerses in parallel, and ensures that there's only a single config
// version initialization.
//

var configRS = new ReplSetTest({name: "configRS", nodes: 3, useHostName: true});
configRS.startSet({configsvr: '', journal: "", storageEngine: 'wiredTiger'});
var replConfig = configRS.getReplSetConfig();
replConfig.configsvr = true;
configRS.initiate(replConfig);

//
// Start a bunch of mongerses which will probably interfere
//

jsTest.log("Starting first set of mongerses in parallel...");

var mongerses = [];
for (var i = 0; i < 3; i++) {
    var mongers = MongerRunner.runMongers(
        {binVersion: "latest", configdb: configRS.getURL(), waitForConnect: false});
    mongerses.push(mongers);
}

// Eventually connect to a monger host, to be sure that the config upgrade happened
// (This can take longer on extremely slow bbots or VMs)
var mongersConn = null;
assert.soon(function() {
    try {
        mongersConn = new Monger(mongerses[0].host);
        return true;
    } catch (e) {
        print("Waiting for connect...");
        printjson(e);
        return false;
    }
}, "Mongers " + mongerses[0].host + " did not start.", 5 * 60 * 1000);

var version = mongersConn.getCollection("config.version").findOne();

//
// Start a second set of mongerses which should respect the initialized version
//

jsTest.log("Starting second set of mongerses...");

for (var i = 0; i < 3; i++) {
    var mongers = MongerRunner.runMongers(
        {binVersion: "latest", configdb: configRS.getURL(), waitForConnect: false});
    mongerses.push(mongers);
}

var connectToMongers = function(host) {
    // Eventually connect to a host
    assert.soon(function() {
        try {
            mongersConn = new Monger(host);
            return true;
        } catch (e) {
            print("Waiting for connect to " + host);
            printjson(e);
            return false;
        }
    }, "mongers " + host + " did not start.", 5 * 60 * 1000);
};

for (var i = 0; i < mongerses.length; i++) {
    connectToMongers(mongerses[i].host);
}

// Shut down our mongerses now that we've tested them
for (var i = 0; i < mongerses.length; i++) {
    MongerRunner.stopMongers(mongerses[i]);
}

//
// Check version and that the version was only updated once
//

assert.eq(5, version.minCompatibleVersion);
assert.eq(6, version.currentVersion);
assert(version.clusterId);
assert.eq(undefined, version.excluding);

var oplog = configRS.getPrimary().getDB('local').oplog.rs;
var updates = oplog.find({ns: "config.version"}).toArray();
assert.eq(1, updates.length, 'ops to config.version: ' + tojson(updates));

configRS.stopSet(15);
