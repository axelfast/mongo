// A mongers participating in a cluster that has been upgraded both a binary and FCV version above it
// should crash.
//
// This kind of scenario can happen when a user forgets to upgrade the mongers binary and then calls
// setFCV(upgrade), leaving the still downgraded mongers unable to communicate. Rather than the
// mongers logging incompatible server version errors endlessly, we've chosen to crash it.

TestData.skipCheckingUUIDsConsistentAcrossCluster = true;

(function() {
    "use strict";

    load("jstests/libs/feature_compatibility_version.js");

    const lastStable = "last-stable";

    let st = new ShardingTest({mongers: 1, shards: 1});
    const ns = "testDB.testColl";
    let mongersAdminDB = st.s.getDB("admin");

    // Assert that a mongers using the 'last-stable' binary version will crash when connecting to a
    // cluster running on the 'latest' binary version with the 'latest' FCV.
    let lastStableMongers =
        MongerRunner.runMongers({configdb: st.configRS.getURL(), binVersion: lastStable});

    assert(!lastStableMongers);

    // Assert that a mongers using the 'last-stable' binary version will successfully connect to a
    // cluster running on the 'latest' binary version with the 'last-stable' FCV.
    assert.commandWorked(mongersAdminDB.runCommand({setFeatureCompatibilityVersion: lastStableFCV}));
    lastStableMongers =
        MongerRunner.runMongers({configdb: st.configRS.getURL(), binVersion: lastStable});
    assert.neq(null,
               lastStableMongers,
               "mongers was unable to start up with binary version=" + lastStable +
                   " and connect to FCV=" + lastStableFCV + " cluster");

    // Ensure that the 'lastStable' binary mongers can perform reads and writes to the shards in the
    // cluster.
    assert.writeOK(lastStableMongers.getDB("test").foo.insert({x: 1}));
    let foundDoc = lastStableMongers.getDB("test").foo.findOne({x: 1});
    assert.neq(null, foundDoc);
    assert.eq(1, foundDoc.x, tojson(foundDoc));

    // Assert that the 'lastStable' binary mongers will crash after the cluster is upgraded to
    // 'latestFCV'.
    assert.commandWorked(mongersAdminDB.runCommand({setFeatureCompatibilityVersion: latestFCV}));
    let error = assert.throws(function() {
        lastStableMongers.getDB("test").foo.insert({x: 1});
    });
    assert(isNetworkError(error));
    assert(!lastStableMongers.conn);

    st.stop();
})();
