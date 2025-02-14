// Checking UUID consistency involves talking to a shard node, which in this test is shutdown
TestData.skipCheckingUUIDsConsistentAcrossCluster = true;

(function() {
    'use strict';

    load("jstests/replsets/rslib.js");

    var s = new ShardingTest({shards: 2, mongers: 1, rs: {oplogSize: 10}});

    var db = s.getDB("test");
    var replTest = s.rs0;

    assert.writeOK(db.foo.insert({_id: 1}));
    db.foo.renameCollection('bar');
    assert.isnull(db.getLastError(), '1.0');
    assert.eq(db.bar.findOne(), {_id: 1}, '1.1');
    assert.eq(db.bar.count(), 1, '1.2');
    assert.eq(db.foo.count(), 0, '1.3');

    assert.writeOK(db.foo.insert({_id: 2}));
    db.foo.renameCollection('bar', true);
    assert.isnull(db.getLastError(), '2.0');
    assert.eq(db.bar.findOne(), {_id: 2}, '2.1');
    assert.eq(db.bar.count(), 1, '2.2');
    assert.eq(db.foo.count(), 0, '2.3');

    assert.commandWorked(s.s0.adminCommand({enablesharding: "test"}));
    s.ensurePrimaryShard('test', s.shard0.shardName);

    // Ensure renaming to or from a sharded collection fails.
    jsTest.log('Testing renaming sharded collections');
    assert.commandWorked(
        s.s0.adminCommand({shardCollection: 'test.shardedColl', key: {_id: 'hashed'}}));

    // Renaming from a sharded collection
    assert.commandFailed(db.shardedColl.renameCollection('somethingElse'));

    // Renaming to a sharded collection
    assert.commandFailed(db.bar.renameCollection('shardedColl'));

    const dropTarget = true;
    assert.commandFailed(db.bar.renameCollection('shardedColl', dropTarget));

    jsTest.log('Testing renaming sharded collections, directly on the shard');
    var primary = replTest.getPrimary();
    assert.commandFailed(primary.getDB('test').shardedColl.renameCollection('somethingElse'));
    assert.commandFailed(primary.getDB('test').bar.renameCollection('shardedColl'));
    assert.commandFailed(primary.getDB('test').bar.renameCollection('shardedColl', dropTarget));

    jsTest.log("Testing write concern (1)");

    assert.writeOK(db.foo.insert({_id: 3}));
    db.foo.renameCollection('bar', true);

    var ans = db.runCommand({getLastError: 1, w: 3});
    printjson(ans);
    assert.isnull(ans.err, '3.0');

    assert.eq(db.bar.findOne(), {_id: 3}, '3.1');
    assert.eq(db.bar.count(), 1, '3.2');
    assert.eq(db.foo.count(), 0, '3.3');

    // Ensure write concern works by shutting down 1 node in a replica set shard
    jsTest.log("Testing write concern (2)");

    // Kill any node. Don't care if it's a primary or secondary.
    replTest.stop(0);

    // Call getPrimary() to populate replTest._slaves.
    replTest.getPrimary();
    let liveSlaves = replTest._slaves.filter(function(node) {
        return node.host !== replTest.nodes[0].host;
    });
    replTest.awaitSecondaryNodes(null, liveSlaves);
    awaitRSClientHosts(s.s, replTest.getPrimary(), {ok: true, ismaster: true}, replTest.name);

    assert.writeOK(db.foo.insert({_id: 4}));
    assert.commandWorked(db.foo.renameCollection('bar', true));

    ans = db.runCommand({getLastError: 1, w: 3, wtimeout: 5000});
    assert.eq(ans.err, "timeout", 'gle: ' + tojson(ans));

    s.stop();
})();
