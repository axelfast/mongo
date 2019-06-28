// Tests whether a reset sharding version triggers errors
(function() {
    'use strict';

    var st = new ShardingTest({shards: 1, mongers: 2});

    var mongersA = st.s0;
    var mongersB = st.s1;

    jsTest.log("Adding new collections...");

    var collA = mongersA.getCollection(jsTestName() + ".coll");
    assert.writeOK(collA.insert({hello: "world"}));

    var collB = mongersB.getCollection("" + collA);
    assert.writeOK(collB.insert({hello: "world"}));

    jsTest.log("Enabling sharding...");

    assert.commandWorked(mongersA.getDB("admin").adminCommand({enableSharding: "" + collA.getDB()}));
    assert.commandWorked(
        mongersA.getDB("admin").adminCommand({shardCollection: "" + collA, key: {_id: 1}}));

    // MongerD doesn't know about the config shard version *until* MongerS tells it
    collA.findOne();

    jsTest.log("Trigger shard version mismatch...");

    assert.writeOK(collB.insert({goodbye: "world"}));

    print("Inserted...");

    assert.eq(3, collA.find().itcount());
    assert.eq(3, collB.find().itcount());

    st.stop();
})();
