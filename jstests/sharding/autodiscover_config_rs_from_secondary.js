// Tests that mongers can autodiscover a config server replica set when the only node it knows about
// is not the primary.

load('jstests/libs/feature_compatibility_version.js');

(function() {
    'use strict';

    var rst = new ReplSetTest(
        {name: "configRS", nodes: 3, nodeOptions: {configsvr: "", storageEngine: "wiredTiger"}});
    rst.startSet();
    var conf = rst.getReplSetConfig();
    conf.members[1].priority = 0;
    conf.members[2].priority = 0;
    conf.writeConcernMajorityJournalDefault = true;
    rst.initiate(conf);

    // Config servers always start at the latest available FCV for the binary. This poses a problem
    // when this test is run in the mixed version suite because mongers will be 'last-stable' and if
    // this node is of the latest binary, it will report itself as the 'latest' FCV, which would
    // cause mongers to refuse to connect to it and shutdown.
    //
    // In order to work around this, in the mixed version suite, be pessimistic and always set this
    // node to the 'last-stable' FCV
    if (jsTestOptions().shardMixedBinVersions) {
        assert.commandWorked(
            rst.getPrimary().adminCommand({setFeatureCompatibilityVersion: lastStableFCV}));
        rst.awaitReplication();
    }

    var seedList = rst.name + "/" + rst.nodes[1].host;  // node 1 is guaranteed to not be primary
    {
        // Ensure that mongers can start up when given the CSRS secondary, discover the primary, and
        // perform writes to the config servers.
        var mongers = MongerRunner.runMongers({configdb: seedList});
        var admin = mongers.getDB('admin');
        assert.writeOK(admin.foo.insert({a: 1}));
        assert.eq(1, admin.foo.findOne().a);
        MongerRunner.stopMongers(mongers);
    }

    // Wait for replication to all config server replica set members to ensure that mongers
    // will be able to do majority reads when trying to verify if the initial cluster metadata
    // has been properly written.
    rst.awaitLastOpCommitted();
    // Now take down the one electable node
    rst.stop(0);
    rst.awaitNoPrimary();

    // Start a mongers when there is no primary
    var mongers = MongerRunner.runMongers({configdb: seedList});
    // Take down the one node the mongers knew about to ensure that it autodiscovered the one
    // remaining
    // config server
    rst.stop(1);

    var admin = mongers.getDB('admin');
    mongers.setSlaveOk(true);
    assert.eq(1, admin.foo.findOne().a);
    MongerRunner.stopMongers(mongers);
    rst.stopSet();
})();
