// Tests whether new sharding is detected on insert by mongers
(function() {

    var st = new ShardingTest({name: "mongers_no_detect_sharding", shards: 1, mongers: 2});

    var mongers = st.s;
    var config = mongers.getDB("config");

    print("Creating unsharded connection...");

    var mongers2 = st._mongers[1];

    var coll = mongers2.getCollection("test.foo");
    assert.writeOK(coll.insert({i: 0}));

    print("Sharding collection...");

    var admin = mongers.getDB("admin");

    assert.eq(coll.getShardVersion().ok, 0);

    admin.runCommand({enableSharding: "test"});
    admin.runCommand({shardCollection: "test.foo", key: {_id: 1}});

    print("Seeing if data gets inserted unsharded...");
    print("No splits occur here!");

    // Insert a bunch of data which should trigger a split
    var bulk = coll.initializeUnorderedBulkOp();
    for (var i = 0; i < 100; i++) {
        bulk.insert({i: i + 1});
    }
    assert.writeOK(bulk.execute());

    st.printShardingStatus(true);

    assert.eq(coll.getShardVersion().ok, 1);
    assert.eq(101, coll.find().itcount());

    st.stop();

})();
