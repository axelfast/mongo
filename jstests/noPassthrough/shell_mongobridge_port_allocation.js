/**
 * Tests that the ports assigned to the mongerbridge and mongerd/mongers processes make it easy to
 * reason about which mongerbridge process corresponds to a particular mongerd/mongers process in the
 * logs.
 *
 * @tags: [requires_replication, requires_sharding]
 */
(function() {
    "use strict";

    function checkBridgeOffset(node, processType) {
        const bridgePort = node.port;
        const serverPort =
            assert.commandWorked(node.adminCommand({getCmdLineOpts: 1})).parsed.net.port;
        assert.neq(bridgePort,
                   serverPort,
                   node + " is a connection to " + processType + " rather than to mongerbridge");
        assert.eq(bridgePort + MongoBridge.kBridgeOffset,
                  serverPort,
                  "corresponding mongerbridge and " + processType +
                      " ports should be staggered by a multiple of 10");
    }

    // We use >5 nodes to ensure that allocating twice as many ports doesn't interfere with having
    // the corresponding mongerbridge and mongerd ports staggered by a multiple of 10.
    const rst = new ReplSetTest({nodes: 7, useBridge: true});
    rst.startSet();

    // Rig the election so that the primary remains stable throughout this test despite the replica
    // set having a larger number of members.
    const replSetConfig = rst.getReplSetConfig();
    for (let i = 1; i < rst.nodes.length; ++i) {
        replSetConfig.members[i].priority = 0;
        replSetConfig.members[i].votes = 0;
    }
    rst.initiate(replSetConfig);

    for (let node of rst.nodes) {
        checkBridgeOffset(node, "mongerd");
    }

    rst.stopSet();

    // We run ShardingTest under mongerbridge with both 1-node replica set shards and stand-alone
    // mongerd shards.
    for (let options of[{rs: {nodes: 1}}, {rs: false, shardAsReplicaSet: false}]) {
        resetAllocatedPorts();

        const numMongos = 5;
        const numShards = 5;
        const st = new ShardingTest(Object.assign({
            mongers: numMongos,
            shards: numShards,
            config: {nodes: 1},
            useBridge: true,
        },
                                                  options));

        for (let i = 0; i < numMongos; ++i) {
            checkBridgeOffset(st["s" + i], "mongers");
        }

        for (let configServer of st.configRS.nodes) {
            checkBridgeOffset(configServer, "config server");
        }

        for (let i = 0; i < numShards; ++i) {
            if (options.rs) {
                for (let node of st["rs" + i].nodes) {
                    checkBridgeOffset(node, "shard");
                }
            } else {
                checkBridgeOffset(st["d" + i], "shard");
            }
        }

        st.stop();
    }
})();
