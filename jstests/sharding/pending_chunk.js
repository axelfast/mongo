//
// Tests pending chunk metadata.
//

(function() {
    "use strict";

    var st = new ShardingTest({shards: 2, mongers: 2, other: {separateConfig: true}});

    var mongers = st.s0;
    var admin = mongers.getDB('admin');
    var coll = mongers.getCollection('foo.bar');
    var ns = coll.getFullName();
    var dbName = coll.getDB().getName();

    assert.commandWorked(admin.runCommand({enableSharding: dbName}));
    printjson(admin.runCommand({movePrimary: dbName, to: st.shard0.shardName}));
    assert.commandWorked(admin.runCommand({shardCollection: ns, key: {_id: 1}}));

    // Turn off best-effort recipient metadata refresh post-migration commit on both shards because
    // it would clean up the pending chunks on migration recipients.
    assert.commandWorked(st.shard0.getDB('admin').runCommand(
        {configureFailPoint: 'doNotRefreshRecipientAfterCommit', mode: 'alwaysOn'}));
    assert.commandWorked(st.shard1.getDB('admin').runCommand(
        {configureFailPoint: 'doNotRefreshRecipientAfterCommit', mode: 'alwaysOn'}));

    jsTest.log('Moving some chunks to shard1...');

    assert.commandWorked(admin.runCommand({split: ns, middle: {_id: 0}}));
    assert.commandWorked(admin.runCommand({split: ns, middle: {_id: 1}}));
    assert.commandWorked(admin.runCommand(
        {moveChunk: ns, find: {_id: 0}, to: st.shard1.shardName, _waitForDelete: true}));
    assert.commandWorked(admin.runCommand(
        {moveChunk: ns, find: {_id: 1}, to: st.shard1.shardName, _waitForDelete: true}));

    function getMetadata(shard) {
        var admin = shard.getDB('admin'),
            metadata = admin.runCommand({getShardVersion: ns, fullMetadata: true}).metadata;

        jsTest.log('Got metadata: ' + tojson(metadata));
        return metadata;
    }

    var metadata = getMetadata(st.shard1);
    assert.eq(metadata.pending[0][0]._id, 1);
    assert.eq(metadata.pending[0][1]._id, MaxKey);

    jsTest.log('Moving some chunks back to shard0 after empty...');

    assert.commandWorked(admin.runCommand(
        {moveChunk: ns, find: {_id: -1}, to: st.shard1.shardName, _waitForDelete: true}));

    metadata = getMetadata(st.shard0);
    assert.eq(metadata.shardVersion.t, 0);
    assert.neq(metadata.collVersion.t, 0);
    assert.eq(metadata.pending.length, 0);

    assert.commandWorked(admin.runCommand(
        {moveChunk: ns, find: {_id: 1}, to: st.shard0.shardName, _waitForDelete: true}));

    metadata = getMetadata(st.shard0);
    assert.eq(metadata.shardVersion.t, 0);
    assert.neq(metadata.collVersion.t, 0);
    assert.eq(metadata.pending[0][0]._id, 1);
    assert.eq(metadata.pending[0][1]._id, MaxKey);

    // The pending chunk should be promoted to a real chunk when shard0 reloads
    // its config.
    jsTest.log('Checking that pending chunk is promoted on reload...');

    assert.eq(null, coll.findOne({_id: 1}));

    metadata = getMetadata(st.shard0);
    assert.neq(metadata.shardVersion.t, 0);
    assert.neq(metadata.collVersion.t, 0);
    assert.eq(metadata.chunks[0][0]._id, 1);
    assert.eq(metadata.chunks[0][1]._id, MaxKey);

    st.printShardingStatus();

    st.stop();

})();
