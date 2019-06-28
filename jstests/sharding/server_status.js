/**
 * Basic test for the 'sharding' section of the serverStatus response object for
 * both mongers and the shard.
 */

(function() {
    "use strict";

    var st = new ShardingTest({shards: 1});

    var testDB = st.s.getDB('test');
    testDB.adminCommand({enableSharding: 'test'});
    testDB.adminCommand({shardCollection: 'test.user', key: {_id: 1}});

    // Initialize shard metadata in shards
    testDB.user.insert({x: 1});

    var checkShardingServerStatus = function(doc) {
        var shardingSection = doc.sharding;
        assert.neq(shardingSection, null);

        var configConnStr = shardingSection.configsvrConnectionString;
        var configConn = new Monger(configConnStr);
        var configIsMaster = configConn.getDB('admin').runCommand({isMaster: 1});

        var configOpTimeObj = shardingSection.lastSeenConfigServerOpTime;

        assert.gt(configConnStr.indexOf('/'), 0);
        assert.gte(configIsMaster.configsvr, 1);  // If it's a shard, this field won't exist.
        assert.neq(null, configOpTimeObj);
        assert.neq(null, configOpTimeObj.ts);
        assert.neq(null, configOpTimeObj.t);

        assert.neq(null, shardingSection.maxChunkSizeInBytes);
    };

    var mongersServerStatus = testDB.adminCommand({serverStatus: 1});
    checkShardingServerStatus(mongersServerStatus);

    var mongerdServerStatus = st.rs0.getPrimary().getDB('admin').runCommand({serverStatus: 1});
    checkShardingServerStatus(mongerdServerStatus);

    st.stop();
})();
