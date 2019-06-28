/**
 * Tests that the monger shell can use a cluster time with a valid signature to advance a server's
 * cluster time.
 */
(function() {
    "use strict";

    // Setup 2 mongers processes with mongerbridge.
    let st = new ShardingTest({shards: 1, mongers: 2, useBridge: true});

    // Sever outgoing communications from the second mongers.
    st.s0.disconnect(st.s1);
    st._configServers.forEach(function(configSvr) {
        configSvr.disconnect(st.s1);
    });

    st._rsObjects.forEach(function(rsNodes) {
        rsNodes.nodes.forEach(function(conn) {
            conn.disconnect(st.s1);
        });
    });

    let connectedDB = st.s0.getDB("test");
    let disconnectedDB = st.s1.getDB("test");

    // Send an insert to the connected mongers to advance its cluster time.
    let res = assert.commandWorked(connectedDB.runCommand({insert: "foo", documents: [{x: 1}]}));

    // Get logicalTime metadata from the connected mongers's response and send it in an isMaster
    // command to the disconnected mongers. isMaster does not require mongers to contact any other
    // servers, so the command should succeed.
    let lt = res.$clusterTime;
    res = assert.commandWorked(
        disconnectedDB.runCommand({isMaster: 1, $clusterTime: lt}),
        "expected the disconnected mongers to accept cluster time: " + tojson(lt));

    // Verify cluster time response from the disconnected mongers matches what was passed.
    assert.eq(lt,
              res.$clusterTime,
              "expected the disconnected mongers to send cluster time: " + tojson(lt) +
                  ", received: " + tojson(res.$clusterTime));

    st.stop();
})();
