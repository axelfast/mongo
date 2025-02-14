// Tests various cases of dropping and recreating collections in the same namespace with multiple
// mongerses
(function() {
    'use strict';

    var st = new ShardingTest({shards: 3, mongers: 3, causallyConsistent: true});

    var config = st.s0.getDB("config");
    var admin = st.s0.getDB("admin");
    var coll = st.s0.getCollection("foo.bar");

    // Use separate mongerses for admin, inserting data, and validating results, so no single-mongers
    // tricks will work
    var staleMongers = st.s1;
    var insertMongers = st.s2;

    var shards = [st.shard0, st.shard1, st.shard2];

    //
    // Test that inserts and queries go to the correct shard even when the collection has been
    // sharded from another mongers
    //

    jsTest.log("Enabling sharding for the first time...");

    assert.commandWorked(admin.runCommand({enableSharding: coll.getDB() + ""}));
    // TODO(PM-85): Make sure we *always* move the primary after collection lifecyle project is
    // complete
    st.ensurePrimaryShard(coll.getDB().getName(), st.shard1.shardName);
    assert.commandWorked(admin.runCommand({shardCollection: coll + "", key: {_id: 1}}));
    st.configRS.awaitLastOpCommitted();  // TODO: Remove after collection lifecyle project (PM-85)

    var bulk = insertMongers.getCollection(coll + "").initializeUnorderedBulkOp();
    for (var i = 0; i < 100; i++) {
        bulk.insert({_id: i, test: "a"});
    }
    assert.writeOK(bulk.execute());
    assert.eq(100, staleMongers.getCollection(coll + "").find({test: "a"}).itcount());

    assert(coll.drop());
    st.configRS.awaitLastOpCommitted();

    //
    // Test that inserts and queries go to the correct shard even when the collection has been
    // resharded from another mongers, with a different key
    //

    jsTest.log("Re-enabling sharding with a different key...");

    st.ensurePrimaryShard(coll.getDB().getName(), st.shard1.shardName);
    assert.commandWorked(coll.ensureIndex({notId: 1}));
    assert.commandWorked(admin.runCommand({shardCollection: coll + "", key: {notId: 1}}));
    st.configRS.awaitLastOpCommitted();

    bulk = insertMongers.getCollection(coll + "").initializeUnorderedBulkOp();
    for (var i = 0; i < 100; i++) {
        bulk.insert({notId: i, test: "b"});
    }
    assert.writeOK(bulk.execute());
    assert.eq(100, staleMongers.getCollection(coll + "").find({test: "b"}).itcount());
    assert.eq(0, staleMongers.getCollection(coll + "").find({test: {$in: ["a"]}}).itcount());

    assert(coll.drop());
    st.configRS.awaitLastOpCommitted();

    //
    // Test that inserts and queries go to the correct shard even when the collection has been
    // unsharded from another mongers
    //

    jsTest.log("Re-creating unsharded collection from a sharded collection...");

    bulk = insertMongers.getCollection(coll + "").initializeUnorderedBulkOp();
    for (var i = 0; i < 100; i++) {
        bulk.insert({test: "c"});
    }
    assert.writeOK(bulk.execute());

    assert.eq(100, staleMongers.getCollection(coll + "").find({test: "c"}).itcount());
    assert.eq(0, staleMongers.getCollection(coll + "").find({test: {$in: ["a", "b"]}}).itcount());

    st.stop();
})();
