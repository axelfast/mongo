/**
 * This test asserts that an illegal OpID passed to mongers' implementation of killOp results in a
 * failure being propagated back to the client.
 */
(function() {
    "use strict";
    var st = new ShardingTest({name: "shard1", shards: 1, mongers: 1});

    assert.commandFailed(st.s.getDB("admin").runCommand(
        {killOp: 1, op: st.shard0.shardName + ":99999999999999999999999"}));
    st.stop();
})();
