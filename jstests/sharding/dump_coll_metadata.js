//
// Tests that we can dump collection metadata via getShardVersion()
//
(function() {
    'use strict';

    var st = new ShardingTest({shards: 2, mongers: 1});

    var mongers = st.s0;
    var coll = mongers.getCollection("foo.bar");
    var admin = mongers.getDB("admin");
    var shardAdmin = st.shard0.getDB("admin");

    assert.commandWorked(admin.runCommand({enableSharding: coll.getDB() + ""}));
    st.ensurePrimaryShard(coll.getDB() + "", st.shard0.shardName);
    assert.commandWorked(admin.runCommand({shardCollection: coll + "", key: {_id: 1}}));

    assert.commandWorked(shardAdmin.runCommand({getShardVersion: coll + ""}));

    // Make sure we have chunks information on the shard after the shard collection call
    var result = assert.commandWorked(
        shardAdmin.runCommand({getShardVersion: coll + "", fullMetadata: true}));
    printjson(result);
    var metadata = result.metadata;

    assert.eq(metadata.chunks.length, 1);
    assert.eq(metadata.pending.length, 0);
    assert.eq(metadata.chunks[0][0]._id, MinKey);
    assert.eq(metadata.chunks[0][1]._id, MaxKey);
    assert.eq(metadata.shardVersion, result.global);

    // Make sure a collection with no metadata still returns the metadata field
    assert.neq(shardAdmin.runCommand({getShardVersion: coll + "xyz", fullMetadata: true}).metadata,
               undefined);

    // Make sure we get multiple chunks after a split and refresh -- splits by themselves do not
    // cause the shard to refresh.
    assert.commandWorked(admin.runCommand({split: coll + "", middle: {_id: 0}}));
    assert.commandWorked(
        st.shard0.getDB('admin').runCommand({_flushRoutingTableCacheUpdates: coll + ""}));

    assert.commandWorked(shardAdmin.runCommand({getShardVersion: coll + ""}));
    printjson(shardAdmin.runCommand({getShardVersion: coll + "", fullMetadata: true}));

    // Make sure we have chunks info
    result = shardAdmin.runCommand({getShardVersion: coll + "", fullMetadata: true});
    assert.commandWorked(result);
    metadata = result.metadata;

    assert.eq(metadata.chunks.length, 2);
    assert.eq(metadata.pending.length, 0);
    assert(metadata.chunks[0][0]._id + "" == MinKey + "");
    assert(metadata.chunks[0][1]._id == 0);
    assert(metadata.chunks[1][0]._id == 0);
    assert(metadata.chunks[1][1]._id + "" == MaxKey + "");
    assert(metadata.shardVersion + "" == result.global + "");

    st.stop();

})();
