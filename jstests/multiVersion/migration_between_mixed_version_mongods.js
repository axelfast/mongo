//
// Testing migrations between latest and last-stable mongerd versions, where the
// donor is the latest version and the recipient the last-stable, and vice versa.
// Migrations should be successful.
//

// Checking UUID consistency involves talking to a shard node, which in this test is shutdown
TestData.skipCheckingUUIDsConsistentAcrossCluster = true;

load("./jstests/multiVersion/libs/verify_versions.js");

(function() {
    "use strict";

    var options = {
        shards: [
            {binVersion: "last-stable"},
            {binVersion: "last-stable"},
            {binVersion: "latest"},
            {binVersion: "latest"}
        ],
        mongers: 1,
        other: {mongersOptions: {binVersion: "last-stable"}, shardAsReplicaSet: false}
    };

    var st = new ShardingTest(options);
    st.stopBalancer();

    assert.binVersion(st.shard0, "last-stable");
    assert.binVersion(st.shard1, "last-stable");
    assert.binVersion(st.shard2, "latest");
    assert.binVersion(st.shard3, "latest");
    assert.binVersion(st.s0, "last-stable");

    var mongers = st.s0, admin = mongers.getDB('admin'),
        shards = mongers.getCollection('config.shards').find().toArray(),

        fooDB = "fooTest", fooNS = fooDB + ".foo", fooColl = mongers.getCollection(fooNS),
        fooDonor = st.shard0, fooRecipient = st.shard2,
        fooDonorColl = fooDonor.getCollection(fooNS),
        fooRecipientColl = fooRecipient.getCollection(fooNS),

        barDB = "barTest", barNS = barDB + ".foo", barColl = mongers.getCollection(barNS),
        barDonor = st.shard3, barRecipient = st.shard1,
        barDonorColl = barDonor.getCollection(barNS),
        barRecipientColl = barRecipient.getCollection(barNS);

    assert.commandWorked(admin.runCommand({enableSharding: fooDB}));
    assert.commandWorked(admin.runCommand({enableSharding: barDB}));
    st.ensurePrimaryShard(fooDB, shards[0]._id);
    st.ensurePrimaryShard(barDB, shards[3]._id);

    assert.commandWorked(admin.runCommand({shardCollection: fooNS, key: {a: 1}}));
    assert.commandWorked(admin.runCommand({split: fooNS, middle: {a: 10}}));
    assert.commandWorked(admin.runCommand({shardCollection: barNS, key: {a: 1}}));
    assert.commandWorked(admin.runCommand({split: barNS, middle: {a: 10}}));

    fooColl.insert({a: 0});
    assert.eq(null, fooColl.getDB().getLastError());
    fooColl.insert({a: 10});
    assert.eq(null, fooColl.getDB().getLastError());
    assert.eq(0, fooRecipientColl.count());
    assert.eq(2, fooDonorColl.count());
    assert.eq(2, fooColl.count());

    barColl.insert({a: 0});
    assert.eq(null, barColl.getDB().getLastError());
    barColl.insert({a: 10});
    assert.eq(null, barColl.getDB().getLastError());
    assert.eq(0, barRecipientColl.count());
    assert.eq(2, barDonorColl.count());
    assert.eq(2, barColl.count());

    /**
     * Perform two migrations:
     *      shard0 (last-stable) -> foo chunk -> shard2 (latest)
     *      shard3 (latest)      -> bar chunk -> shard1 (last-stable)
     */

    assert.commandWorked(admin.runCommand(
        {moveChunk: fooNS, find: {a: 10}, to: shards[2]._id, _waitForDelete: true}));
    assert.commandWorked(admin.runCommand(
        {moveChunk: barNS, find: {a: 10}, to: shards[1]._id, _waitForDelete: true}));
    assert.eq(1,
              fooRecipientColl.count(),
              "Foo collection migration failed. " +
                  "Last-stable -> latest mongerd version migration failure.");
    assert.eq(1,
              fooDonorColl.count(),
              "Foo donor lost its document. " +
                  "Last-stable -> latest mongerd version migration failure.");
    assert.eq(2,
              fooColl.count(),
              "Incorrect number of documents in foo collection. " +
                  "Last-stable -> latest mongerd version migration failure.");
    assert.eq(1,
              barRecipientColl.count(),
              "Bar collection migration failed. " +
                  "Latest -> last-stable mongerd version migration failure.");
    assert.eq(1,
              barDonorColl.count(),
              "Bar donor lost its document. " +
                  "Latest -> last-stable mongerd version migration failure.");
    assert.eq(2,
              barColl.count(),
              "Incorrect number of documents in bar collection. " +
                  "Latest -> last-stable mongerd version migration failure.");

    st.stop();
})();
