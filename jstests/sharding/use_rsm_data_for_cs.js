(function() {
    'use strict';

    // init with one shard with one node rs
    var st = new ShardingTest({shards: 1, rs: {nodes: 1}, mongers: 1});
    var mongers = st.s;
    var rs = st.rs0;

    assert.commandWorked(st.s0.adminCommand({enablesharding: "test"}));

    var db = mongers.getDB("test");
    db.foo.save({_id: 1, x: 1});
    assert.eq(db.foo.find({_id: 1}).next().x, 1);

    // prevent RSM on all nodes to update config shard
    mongers.adminCommand(
        {configureFailPoint: "failReplicaSetChangeConfigServerUpdateHook", mode: "alwaysOn"});
    rs.nodes.forEach(function(node) {
        node.adminCommand(
            {configureFailPoint: "failUpdateShardIdentityConfigString", mode: "alwaysOn"});
    });

    // add a node to shard rs
    rs.add({'shardsvr': ''});
    rs.reInitiate();
    rs.awaitSecondaryNodes();

    jsTest.log("Reload ShardRegistry");
    // force SR reload with flushRouterConfig
    mongers.getDB("admin").runCommand({flushRouterConfig: 1});

    // issue a read from mongers with secondaryOnly read preference to force it use just added node
    jsTest.log("Issue find");
    assert.eq(db.foo.find({_id: 1}).readPref('secondary').next().x, 1);

    st.stop();
})();
