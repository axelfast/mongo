// Tests that pipeline optimization works properly when the failpoint isn't triggered, and is
// disabled properly when it is triggered.
(function() {
    "use strict";

    load("jstests/libs/analyze_plan.js");  // For aggPlan functions.
    Random.setRandomSeed();

    const conn = MongerRunner.runMongerd({});
    assert.neq(conn, null, "Mongerd failed to start up.");
    const testDb = conn.getDB("test");
    const coll = testDb.agg_opt;

    const pops = new Set();
    for (let i = 0; i < 25; ++i) {
        let pop;
        do {
            pop = Random.randInt(100000);
        } while (pops.has(pop));
        pops.add(pop);

        assert.commandWorked(coll.insert({_id: i, city: "Cleveland", pop: pop, state: "OH"}));
    }

    const pipeline = [{$match: {state: "OH"}}, {$sort: {pop: -1}}, {$limit: 10}];

    const enabledPlan = coll.explain().aggregate(pipeline);
    // Test that sort and the limit were combined.
    assert.eq(aggPlanHasStage(enabledPlan, "$limit"), false);
    assert.eq(aggPlanHasStage(enabledPlan, "$sort"), true);
    assert.eq(enabledPlan.stages.length, 2);

    const enabledResult = coll.aggregate(pipeline).toArray();

    // Enable a failpoint that will cause pipeline optimizations to be skipped.
    assert.commandWorked(
        testDb.adminCommand({configureFailPoint: "disablePipelineOptimization", mode: "alwaysOn"}));

    const disabledPlan = coll.explain().aggregate(pipeline);
    // Test that the $limit still exists and hasn't been optimized away.
    assert.eq(aggPlanHasStage(disabledPlan, "$limit"), true);
    assert.eq(aggPlanHasStage(disabledPlan, "$sort"), true);
    assert.eq(disabledPlan.stages.length, 3);

    const disabledResult = coll.aggregate(pipeline).toArray();

    // Test that the result is the same with and without optimizations enabled.
    assert.eq(enabledResult, disabledResult);

    MongerRunner.stopMongerd(conn);
}());
