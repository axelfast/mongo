// Tests the behavior of a $lookup when the mongers contains stale routing information for the
// local and/or foreign collections.  This includes when mongers thinks the collection is sharded
// when it's not, and likewise when mongers thinks the collection is unsharded but is actually
// sharded.
(function() {
    "use strict";

    load("jstests/noPassthrough/libs/server_parameter_helpers.js");  // For setParameterOnAllHosts.
    load("jstests/libs/discover_topology.js");                       // For findDataBearingNodes.

    const testName = "lookup_stale_mongers";
    const st = new ShardingTest({
        shards: 2,
        mongers: 2,
    });
    setParameterOnAllHosts(DiscoverTopology.findNonConfigNodes(st.s0).concat([st.s1.host]),
                           "internalQueryAllowShardedLookup",
                           true);

    const mongers0DB = st.s0.getDB(testName);
    assert.commandWorked(mongers0DB.dropDatabase());
    const mongers0LocalColl = mongers0DB[testName + "_local"];
    const mongers0ForeignColl = mongers0DB[testName + "_foreign"];

    const mongers1DB = st.s1.getDB(testName);
    const mongers1LocalColl = mongers1DB[testName + "_local"];
    const mongers1ForeignColl = mongers1DB[testName + "_foreign"];

    const pipeline = [
        {
          $lookup:
              {localField: "a", foreignField: "b", from: mongers1ForeignColl.getName(), as: "same"}
        },
        {$sort: {_id: 1}}
    ];
    const expectedResults = [
        {_id: 0, a: 1, "same": [{_id: 0, b: 1}]},
        {_id: 1, a: null, "same": [{_id: 1, b: null}, {_id: 2}]},
        {_id: 2, "same": [{_id: 1, b: null}, {_id: 2}]}
    ];

    // Ensure that shard0 is the primary shard.
    assert.commandWorked(mongers0DB.adminCommand({enableSharding: mongers0DB.getName()}));
    st.ensurePrimaryShard(mongers0DB.getName(), st.shard0.shardName);

    assert.writeOK(mongers0LocalColl.insert({_id: 0, a: 1}));
    assert.writeOK(mongers0LocalColl.insert({_id: 1, a: null}));

    assert.writeOK(mongers0ForeignColl.insert({_id: 0, b: 1}));
    assert.writeOK(mongers0ForeignColl.insert({_id: 1, b: null}));

    // Send writes through mongers1 such that it's aware of the collections and believes they are
    // unsharded.
    assert.writeOK(mongers1LocalColl.insert({_id: 2}));
    assert.writeOK(mongers1ForeignColl.insert({_id: 2}));

    //
    // Test unsharded local and sharded foreign collections, with mongers unaware that the foreign
    // collection is sharded.
    //

    // Shard the foreign collection through mongers0.
    assert.commandWorked(
        mongers0DB.adminCommand({shardCollection: mongers0ForeignColl.getFullName(), key: {_id: 1}}));

    // Split the collection into 2 chunks: [MinKey, 1), [1, MaxKey).
    assert.commandWorked(
        mongers0DB.adminCommand({split: mongers0ForeignColl.getFullName(), middle: {_id: 1}}));

    // Move the [minKey, 1) chunk to shard1.
    assert.commandWorked(mongers0DB.adminCommand({
        moveChunk: mongers0ForeignColl.getFullName(),
        find: {_id: 0},
        to: st.shard1.shardName,
        _waitForDelete: true
    }));

    // Issue a $lookup through mongers1, which is unaware that the foreign collection is sharded.
    assert.eq(mongers1LocalColl.aggregate(pipeline).toArray(), expectedResults);

    //
    // Test sharded local and sharded foreign collections, with mongers unaware that the local
    // collection is sharded.
    //

    // Shard the local collection through mongers0.
    assert.commandWorked(
        mongers0DB.adminCommand({shardCollection: mongers0LocalColl.getFullName(), key: {_id: 1}}));

    // Split the collection into 2 chunks: [MinKey, 1), [1, MaxKey).
    assert.commandWorked(
        mongers0DB.adminCommand({split: mongers0LocalColl.getFullName(), middle: {_id: 1}}));

    // Move the [minKey, 1) chunk to shard1.
    assert.commandWorked(mongers0DB.adminCommand({
        moveChunk: mongers0LocalColl.getFullName(),
        find: {_id: 0},
        to: st.shard1.shardName,
        _waitForDelete: true
    }));

    // Issue a $lookup through mongers1, which is unaware that the local collection is sharded.
    assert.eq(mongers1LocalColl.aggregate(pipeline).toArray(), expectedResults);

    //
    // Test sharded local and unsharded foreign collections, with mongers unaware that the foreign
    // collection is unsharded.
    //

    // Recreate the foreign collection as unsharded through mongers0.
    mongers0ForeignColl.drop();
    assert.writeOK(mongers0ForeignColl.insert({_id: 0, b: 1}));
    assert.writeOK(mongers0ForeignColl.insert({_id: 1, b: null}));
    assert.writeOK(mongers0ForeignColl.insert({_id: 2}));

    // Issue a $lookup through mongers1, which is unaware that the foreign collection is now
    // unsharded.
    assert.eq(mongers1LocalColl.aggregate(pipeline).toArray(), expectedResults);

    //
    // Test unsharded local and foreign collections, with mongers unaware that the local
    // collection is unsharded.
    //

    // Recreate the local collection as unsharded through mongers0.
    mongers0LocalColl.drop();
    assert.writeOK(mongers0LocalColl.insert({_id: 0, a: 1}));
    assert.writeOK(mongers0LocalColl.insert({_id: 1, a: null}));
    assert.writeOK(mongers0LocalColl.insert({_id: 2}));

    // Issue a $lookup through mongers1, which is unaware that the local collection is now
    // unsharded.
    assert.eq(mongers1LocalColl.aggregate(pipeline).toArray(), expectedResults);

    st.stop();
})();
