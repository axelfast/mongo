// This test confirms that mongers interprets host names passed to it via the
// --configdb command line argument *without* a port number as referring to
// processes listening on the config server port (27019) rather than the default
// mongerd port of 27017.
//
// That is, mongers --configdb=localhost should look for a config server on port 27019,
// not port 27017.
//
// The test confirms this behavior for 1-node config servers, SCCC config servers and
// CSRS config servers.

(function() {
    "use strict";

    function getHostPart(hostAndPort) {
        return hostAndPort.substr(0, hostAndPort.lastIndexOf(':'));
    }
    var c1, c2, c3;

    // The config servers must support readConcern: majority to be run as a replica set, so
    // explicitly set storage engine to wiredTiger.
    c1 = MongerRunner.runMongerd(
        {configsvr: "", port: 27019, replSet: "csrs", storageEngine: "wiredTiger"});
    assert.commandWorked(c1.adminCommand("replSetInitiate"));
    c2 = MongerRunner.runMongerd({configsvr: "", storageEngine: "wiredTiger"});
    c3 = MongerRunner.runMongerd({configsvr: "", storageEngine: "wiredTiger"});

    assert(MongerRunner.runMongers({configdb: "csrs/" + getHostPart(c1.host)}));
}());
