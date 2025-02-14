/**
 * Verifies that the $collStats aggregation stage includes the shard and hostname for each output
 * document when run via mongerS, and that the former is absent when run on a non-shard mongerD.
 * @tags: [
 *   requires_sharding,
 *   requires_spawning_own_processes,
 * ]
 */
(function() {
    "use strict";

    // Test mongerD behaviour using the standalone started by resmoke.py.
    let testDB = db.getSiblingDB(jsTestName());
    let testColl = testDB.test;

    // getHostName() doesn't include port, db.getMonger().host is 127.0.0.1:<port>
    const hostName = (getHostName() + ":" + db.getMonger().host.split(":")[1]);

    // Test that the shard field is absent and the host field is present when run on mongerD.
    assert.eq(testColl
                  .aggregate([
                      {$collStats: {latencyStats: {histograms: true}}},
                      {$group: {_id: {shard: "$shard", host: "$host"}}}
                  ])
                  .toArray(),
              [{_id: {host: hostName}}]);

    // Test that both shard and hostname are present for $collStats results on a sharded cluster.
    const st = new ShardingTest({name: jsTestName(), shards: 2});

    testDB = st.s.getDB(jsTestName());
    testColl = testDB.test;

    assert.commandWorked(testDB.dropDatabase());

    // Enable sharding on the test database.
    assert.commandWorked(testDB.adminCommand({enableSharding: testDB.getName()}));

    // Shard 'testColl' on {_id: 'hashed'}. This will automatically presplit the collection and
    // place chunks on each shard.
    assert.commandWorked(
        testDB.adminCommand({shardCollection: testColl.getFullName(), key: {_id: "hashed"}}));

    // Group $collStats result by $shard and $host to confirm that both fields are present.
    assert.eq(testColl
                  .aggregate([
                      {$collStats: {latencyStats: {histograms: true}}},
                      {$group: {_id: {shard: "$shard", host: "$host"}}},
                      {$sort: {_id: 1}}
                  ])
                  .toArray(),
              [
                {_id: {shard: st.shard0.shardName, host: st.rs0.getPrimary().host}},
                {_id: {shard: st.shard1.shardName, host: st.rs1.getPrimary().host}},
              ]);

    st.stop();
})();
