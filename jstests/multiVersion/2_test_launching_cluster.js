//
// Tests launching multi-version ShardingTest clusters.
//
// We cannot test with the shards being replica sets. If the 'replSetInitiate' command goes to a
// 3.6 node, then the node will initiate in fCV 3.6 and refuse to talk to the 3.4 node. If the
// 'replSetInitiate' command goes to a 3.4 node, then the node will not write it's fCV document
// at initiation and the 3.6 node will refuse to initial sync from it. For 3.8, we will be able
// to send 'replSetInitiate' to the 3.6 node and it will write the document at initiate in fCV
// 3.6 (since this changed between 3.4 and 3.6), and the 3.8 node will initial sync from it.
// TODO(SERVER-33180) update this test to use replica set shards.
//

load('./jstests/multiVersion/libs/verify_versions.js');

(function() {
    "use strict";
    // Check our latest versions
    var versionsToCheck = ["last-stable", "latest"];
    var versionsToCheckConfig = ["latest"];
    var versionsToCheckMongers = ["last-stable"];

    jsTest.log("Testing mixed versions...");

    // Set up a multi-version cluster
    var st = new ShardingTest({
        shards: 2,
        mongers: 2,
        other: {
            mongersOptions: {binVersion: versionsToCheckMongers},
            configOptions: {binVersion: versionsToCheckConfig},
            shardOptions: {binVersion: versionsToCheck},
            enableBalancer: true,
            shardAsReplicaSet: false
        }
    });

    var shards = [st.shard0, st.shard1];
    var mongerses = [st.s0, st.s1];
    var configs = [st.config0, st.config1, st.config2];

    // Make sure we have hosts of all the different versions
    var versionsFound = [];
    for (var j = 0; j < shards.length; j++)
        versionsFound.push(shards[j].getBinVersion());

    assert.allBinVersions(versionsToCheck, versionsFound);

    versionsFound = [];
    for (var j = 0; j < mongerses.length; j++)
        versionsFound.push(mongerses[j].getBinVersion());

    assert.allBinVersions(versionsToCheckMongers, versionsFound);

    versionsFound = [];
    for (var j = 0; j < configs.length; j++)
        versionsFound.push(configs[j].getBinVersion());

    assert.allBinVersions(versionsToCheckConfig, versionsFound);

    st.stop();
})();
