/**
 * Tests what values are accepted for the maxAcceptableLogicalClockDriftSecs startup parameter, and
 * that servers in a sharded clusters reject cluster times more than
 * maxAcceptableLogicalClockDriftSecs ahead of their wall clocks.
 * @tags: [requires_sharding]
 */
(function() {
    "use strict";

    // maxAcceptableLogicalClockDriftSecs cannot be negative, zero, or a non-number.
    let conn = MongerRunner.runMongerd({setParameter: {maxAcceptableLogicalClockDriftSecs: -1}});
    assert.eq(null, conn, "expected server to reject negative maxAcceptableLogicalClockDriftSecs");

    conn = MongerRunner.runMongerd({setParameter: {maxAcceptableLogicalClockDriftSecs: 0}});
    assert.eq(null, conn, "expected server to reject zero maxAcceptableLogicalClockDriftSecs");

    conn = MongerRunner.runMongerd({setParameter: {maxAcceptableLogicalClockDriftSecs: "value"}});
    assert.eq(
        null, conn, "expected server to reject non-numeric maxAcceptableLogicalClockDriftSecs");

    conn = MongerRunner.runMongerd(
        {setParameter: {maxAcceptableLogicalClockDriftSecs: new Timestamp(50, 0)}});
    assert.eq(
        null, conn, "expected server to reject non-numeric maxAcceptableLogicalClockDriftSecs");

    // Any positive number is valid.
    conn = MongerRunner.runMongerd({setParameter: {maxAcceptableLogicalClockDriftSecs: 1}});
    assert.neq(null, conn, "failed to start mongerd with valid maxAcceptableLogicalClockDriftSecs");
    MongerRunner.stopMongerd(conn);

    conn = MongerRunner.runMongerd({
        setParameter: {maxAcceptableLogicalClockDriftSecs: 60 * 60 * 24 * 365 * 10}
    });  // 10 years.
    assert.neq(null, conn, "failed to start mongerd with valid maxAcceptableLogicalClockDriftSecs");
    MongerRunner.stopMongerd(conn);

    // Verify maxAcceptableLogicalClockDriftSecs works as expected in a sharded cluster.
    const maxDriftValue = 100;
    const st = new ShardingTest({
        shards: 1,
        shardOptions: {setParameter: {maxAcceptableLogicalClockDriftSecs: maxDriftValue}},
        mongersOptions: {setParameter: {maxAcceptableLogicalClockDriftSecs: maxDriftValue}}
    });
    let testDB = st.s.getDB("test");

    // Contact cluster to get initial cluster time.
    let res = assert.commandWorked(testDB.runCommand({isMaster: 1}));
    let lt = res.$clusterTime;

    // Try to advance cluster time by more than the max acceptable drift, which should fail the rate
    // limiter.
    let tooFarTime = Object.assign(
        {}, lt, {clusterTime: new Timestamp(lt.clusterTime.getTime() + (maxDriftValue * 2), 0)});
    assert.commandFailedWithCode(testDB.runCommand({isMaster: 1, $clusterTime: tooFarTime}),
                                 ErrorCodes.ClusterTimeFailsRateLimiter,
                                 "expected command to not pass the rate limiter");

    st.stop();
})();
