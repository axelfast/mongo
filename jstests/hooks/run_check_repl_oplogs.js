// Runner for checkOplogs() that compares the oplog on all replica set nodes
// to ensure all nodes have the same data.
'use strict';

(function() {
    var startTime = Date.now();
    assert.neq(typeof db, 'undefined', 'No `db` object, is the shell connected to a mongerd?');

    let runCheckOnReplSet = function(db) {
        let primaryInfo = db.isMaster();

        assert(primaryInfo.ismaster,
               'shell is not connected to the primary or master node: ' + tojson(primaryInfo));

        let testFixture = new ReplSetTest(db.getMonger().host);
        testFixture.checkOplogs();
    };

    if (db.getMonger().isMongers()) {
        let configDB = db.getSiblingDB('config');

        // Run check on every shard.
        configDB.shards.find().forEach(shardEntry => {
            let newConn = new Monger(shardEntry.host);
            runCheckOnReplSet(newConn.getDB('test'));
        });

        // Run check on config server.
        let cmdLineOpts = db.adminCommand({getCmdLineOpts: 1});
        let configConn = new Monger(cmdLineOpts.parsed.sharding.configDB);
        runCheckOnReplSet(configConn.getDB('test'));
    } else {
        runCheckOnReplSet(db);
    }

    var totalTime = Date.now() - startTime;
    print('Finished consistency oplog checks of cluster in ' + totalTime + ' ms.');
})();
