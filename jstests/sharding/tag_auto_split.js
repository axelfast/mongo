// Test to make sure that tag ranges get split when full keys are used for the tag ranges
(function() {
    'use strict';

    var s = new ShardingTest({shards: 2, mongers: 1});

    assert.commandWorked(s.s0.adminCommand({enablesharding: "test"}));
    s.ensurePrimaryShard('test', s.shard1.shardName);
    assert.commandWorked(s.s0.adminCommand({shardcollection: "test.foo", key: {_id: 1}}));

    assert.eq(1, s.config.chunks.find({"ns": "test.foo"}).itcount());

    s.addShardTag(s.shard0.shardName, "a");
    s.addShardTag(s.shard0.shardName, "b");

    s.addTagRange("test.foo", {_id: 5}, {_id: 10}, "a");
    s.addTagRange("test.foo", {_id: 10}, {_id: 15}, "b");

    s.startBalancer();

    assert.soon(function() {
        return s.config.chunks.find({"ns": "test.foo"}).itcount() == 4;
    }, 'Split did not occur', 3 * 60 * 1000);

    s.awaitBalancerRound();
    s.printShardingStatus(true);
    assert.eq(4, s.config.chunks.find({"ns": "test.foo"}).itcount(), 'Split points changed');

    assert.eq(1, s.config.chunks.find({"ns": "test.foo", min: {_id: MinKey}}).itcount());
    assert.eq(1, s.config.chunks.find({"ns": "test.foo", min: {_id: 5}}).itcount());
    assert.eq(1, s.config.chunks.find({"ns": "test.foo", min: {_id: 10}}).itcount());
    assert.eq(1, s.config.chunks.find({"ns": "test.foo", min: {_id: 15}}).itcount());

    s.stop();
})();
