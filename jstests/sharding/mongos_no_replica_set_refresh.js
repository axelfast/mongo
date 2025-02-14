// Tests that mongers and the shard discover changes to the shard's replica set membership.
load("jstests/replsets/rslib.js");

(function() {
    'use strict';

    var five_minutes = 5 * 60 * 1000;

    var numRSHosts = function() {
        var result = assert.commandWorked(rsObj.nodes[0].adminCommand({ismaster: 1}));
        return result.hosts.length + result.passives.length;
    };

    var numMongersHosts = function() {
        var commandResult = assert.commandWorked(mongers.adminCommand("connPoolStats"));
        var result = commandResult.replicaSets[rsObj.name];
        return result.hosts.length;
    };

    var configServerURL = function() {
        var result = config.shards.find().toArray()[0];
        return result.host;
    };

    var checkNumHosts = function(expectedNumHosts) {
        jsTest.log("Waiting for the shard to discover that it now has " + expectedNumHosts +
                   " hosts.");
        var numHostsSeenByShard;

        // Use a high timeout (5 minutes) because replica set refreshes are only done every 30
        // seconds.
        assert.soon(
            function() {
                numHostsSeenByShard = numRSHosts();
                return numHostsSeenByShard === expectedNumHosts;
            },
            function() {
                return ("Expected shard to see " + expectedNumHosts + " hosts but found " +
                        numHostsSeenByShard);
            },
            five_minutes);

        jsTest.log("Waiting for the mongers to discover that the shard now has " + expectedNumHosts +
                   " hosts.");
        var numHostsSeenByMongers;

        // Use a high timeout (5 minutes) because replica set refreshes are only done every 30
        // seconds.
        assert.soon(
            function() {
                numHostsSeenByMongers = numMongersHosts();
                return numHostsSeenByMongers === expectedNumHosts;
            },
            function() {
                return ("Expected mongers to see " + expectedNumHosts +
                        " hosts on shard but found " + numHostsSeenByMongers);
            },
            five_minutes);
    };

    var st = new ShardingTest({
        name: 'mongers_no_replica_set_refresh',
        shards: 1,
        mongers: 1,
        other: {
            rs0: {
                nodes: [
                    {},
                    {rsConfig: {priority: 0}},
                    {rsConfig: {priority: 0}},
                ],
            }
        }
    });

    var rsObj = st.rs0;
    assert.commandWorked(rsObj.nodes[0].adminCommand({
        replSetTest: 1,
        waitForMemberState: ReplSetTest.State.PRIMARY,
        timeoutMillis: 60 * 1000,
    }),
                         'node 0 ' + rsObj.nodes[0].host + ' failed to become primary');

    var mongers = st.s;
    var config = mongers.getDB("config");

    printjson(mongers.getCollection("foo.bar").findOne());

    jsTestLog("Removing a node from the shard's replica set.");

    var rsConfig = rsObj.getReplSetConfigFromNode(0);

    var removedNode = rsConfig.members.pop();
    rsConfig.version++;
    reconfig(rsObj, rsConfig);

    // Wait for the election round to complete
    rsObj.getPrimary();

    checkNumHosts(rsConfig.members.length);

    jsTest.log("Waiting for config.shards to reflect that " + removedNode.host +
               " has been removed.");
    assert.soon(
        function() {
            return configServerURL().indexOf(removedNode.host) < 0;
        },
        function() {
            return (removedNode.host + " was removed from " + rsObj.name +
                    ", but is still seen in config.shards");
        });

    jsTestLog("Adding the node back to the shard's replica set.");

    config.shards.update({_id: rsObj.name}, {$set: {host: rsObj.name + "/" + rsObj.nodes[0].host}});
    printjson(config.shards.find().toArray());

    rsConfig.members.push(removedNode);
    rsConfig.version++;
    reconfig(rsObj, rsConfig);

    checkNumHosts(rsConfig.members.length);

    jsTest.log("Waiting for config.shards to reflect that " + removedNode.host +
               " has been re-added.");
    assert.soon(
        function() {
            return configServerURL().indexOf(removedNode.host) >= 0;
        },
        function() {
            return (removedNode.host + " was re-added to " + rsObj.name +
                    ", but is not seen in config.shards");
        });

    st.stop();

}());
