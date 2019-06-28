/**
 * Test that a mongers-only aggregation pipeline is explainable, and that the resulting explain plan
 * confirms that the pipeline ran entirely on mongerS.
 */
(function() {
    "use strict";

    const st = new ShardingTest({name: "mongers_comment_test", mongers: 1, shards: 1});
    const mongersConn = st.s;

    const stageSpec = {
        "$listLocalSessions": {allUsers: false, users: [{user: "nobody", db: "nothing"}]}
    };

    // Use the test stage to create a pipeline that runs exclusively on mongerS.
    const mongersOnlyPipeline = [stageSpec, {$match: {dummyField: 1}}];

    // We expect the explain output to reflect the stage's spec.
    const expectedExplainStages = [stageSpec, {$match: {dummyField: {$eq: 1}}}];

    // Test that the mongerS-only pipeline is explainable.
    const explainPlan = assert.commandWorked(mongersConn.getDB("admin").runCommand(
        {aggregate: 1, pipeline: mongersOnlyPipeline, explain: true}));

    // We expect the stages to appear under the 'mongers' heading, for 'splitPipeline' to be
    // null, and for the 'mongers.host' field to be the hostname:port of the mongerS itself.
    assert.docEq(explainPlan.mongers.stages, expectedExplainStages);
    assert.eq(explainPlan.mongers.host, mongersConn.name);
    assert.isnull(explainPlan.splitPipeline);

    st.stop();
})();
