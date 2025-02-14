// SERVER-35132 Test that we still honor maxTimeMs during replica set targeting.
// @tags: [requires_replication]
(function() {
    'use strict';
    var st = new ShardingTest({mongers: 1, shards: 1, rs: {nodes: 2}});
    var kDbName = 'test';
    var ns = 'test.foo';
    var mongers = st.s0;
    var testColl = mongers.getCollection(ns);

    assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));

    // Since this test is timing sensitive, retry on failures since they could be transient.
    // If broken, this would *always* fail so if it ever passes this build is fine (or time went
    // backwards).
    const tryFiveTimes = function(name, f) {
        jsTestLog(`Starting test ${name}`);

        for (var trial = 1; trial <= 5; trial++) {
            try {
                f();
            } catch (e) {
                if (trial < 5) {
                    jsTestLog(`Ignoring error during trial ${trial} of test ${name}`);
                    continue;
                }

                jsTestLog(`Failed 5 times in test ${name}. There is probably a bug here.`);
                throw e;
            }
        }
    };

    const runTest = function() {
        // Sanity Check
        assert.eq(testColl.find({_id: 1}).next(), {_id: 1});

        // MaxTimeMS with satisfiable readPref
        assert.eq(testColl.find({_id: 1}).readPref("secondary").maxTimeMS(1000).next(), {_id: 1});

        let ex = null;

        // MaxTimeMS with unsatisfiable readPref
        const time = Date.timeFunc(() => {
            ex = assert.throws(() => {
                testColl.find({_id: 1})
                    .readPref("secondary", [{tag: "noSuchTag"}])
                    .maxTimeMS(1000)
                    .next();
            });
        });

        assert.gte(time, 1000);      // Make sure we at least waited 1 second.
        assert.lt(time, 15 * 1000);  // We used to wait 20 seconds before timing out.

        assert.eq(ex.code, ErrorCodes.MaxTimeMSExpired);
    };

    testColl.insert({_id: 1});
    tryFiveTimes("totally unsharded", runTest);

    assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));
    tryFiveTimes("sharded db", runTest);

    assert.commandWorked(mongers.adminCommand({shardCollection: ns, key: {_id: 1}}));
    tryFiveTimes("sharded collection", runTest);

    st.stop();
})();
