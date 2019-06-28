// Make sure the setFeatureCompatibilityVersion command respects maxTimeMs.
(function() {
    'use strict';
    load("./jstests/libs/feature_compatibility_version.js");
    var st = new ShardingTest({shards: 2});

    var mongers = st.s0;
    var shards = [st.shard0, st.shard1];
    var coll = mongers.getCollection("foo.bar");
    var admin = mongers.getDB("admin");
    var cursor;
    var res;

    // Helper function to configure "maxTimeAlwaysTimeOut" fail point on shards, which forces mongerd
    // to throw if it receives an operation with a max time.  See fail point declaration for
    // complete description.
    var configureMaxTimeAlwaysTimeOut = function(mode) {
        assert.commandWorked(shards[0].getDB("admin").runCommand(
            {configureFailPoint: "maxTimeAlwaysTimeOut", mode: mode}));
        assert.commandWorked(shards[1].getDB("admin").runCommand(
            {configureFailPoint: "maxTimeAlwaysTimeOut", mode: mode}));
    };

    // Positive test for "setFeatureCompatibilityVersion"
    configureMaxTimeAlwaysTimeOut("alwaysOn");
    assert.commandFailedWithCode(
        admin.runCommand(
            {setFeatureCompatibilityVersion: lastStableFCV, maxTimeMS: 1000 * 60 * 60 * 24}),
        ErrorCodes.MaxTimeMSExpired,
        "expected setFeatureCompatibilityVersion to fail due to maxTimeAlwaysTimeOut fail point");

    // Negative test for "setFeatureCompatibilityVersion"
    configureMaxTimeAlwaysTimeOut("off");
    assert.commandWorked(
        admin.runCommand(
            {setFeatureCompatibilityVersion: lastStableFCV, maxTimeMS: 1000 * 60 * 60 * 24}),
        "expected setFeatureCompatibilityVersion to not hit time limit in mongerd");

    assert.commandWorked(
        admin.runCommand(
            {setFeatureCompatibilityVersion: latestFCV, maxTimeMS: 1000 * 60 * 60 * 24}),
        "expected setFeatureCompatibilityVersion to not hit time limit in mongerd");

    st.stop();
})();
