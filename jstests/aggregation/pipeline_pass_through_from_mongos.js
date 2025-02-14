/**
 * Tests to verify that the aggregation pipeline passthrough behaviour works as expected for stages
 * which have sub-pipelines, whose stages may have differing passthrough constraints. This test
 * exercises the fix for SERVER-41290.
 * @tags: [requires_sharding]
 */
(function() {
    'use strict';

    load("jstests/libs/profiler.js");  // For profilerHas*OrThrow helper functions.

    const st = new ShardingTest({shards: 2});
    const mongersDB = st.s0.getDB(jsTestName());
    assert.commandWorked(st.s0.adminCommand({enableSharding: jsTestName()}));
    st.ensurePrimaryShard(jsTestName(), st.shard0.shardName);
    const mongersColl = mongersDB.test;
    const primaryShard = st.shard0.getDB(jsTestName());
    const shard1DB = st.shard1.getDB(jsTestName());

    assert.commandWorked(primaryShard.setProfilingLevel(2));
    assert.commandWorked(shard1DB.setProfilingLevel(2));

    // Verify that the $lookup is passed through to the primary shard when all its sub-pipeline
    // stages can be passed through.
    let testName = "sub_pipeline_can_be_passed_through";
    assert.commandWorked(mongersDB.runCommand({
        aggregate: mongersColl.getName(),
        pipeline: [{
            $lookup:
                {pipeline: [{$match: {a: "val"}}], from: mongersDB.otherColl.getName(), as: "c"}
        }],
        cursor: {},
        comment: testName
    }));
    profilerHasSingleMatchingEntryOrThrow({
        profileDB: primaryShard,
        filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
    });
    profilerHasZeroMatchingEntriesOrThrow({
        profileDB: shard1DB,
        filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
    });

    // Test to verify that the mongerS doesn't pass the pipeline through to the primary shard when
    // $lookup's sub-pipeline has one or more stages which don't allow passthrough. In this
    // sub-pipeline, the $merge stage is not allowed to pass through, which forces the pipeline to
    // be parsed on mongerS. Since $merge is not allowed within a $lookup, the command thus fails on
    // mongerS without ever reaching a shard. This test-case exercises the bug described in
    // SERVER-41290.
    const pipelineForLookup = [
        {
          $lookup: {
              pipeline: [{$match: {a: "val"}}, {$merge: {into: "merge_collection"}}],
              from: mongersDB.otherColl.getName(),
              as: "c",
          }
        },
    ];
    testName = "lookup_with_merge_cannot_be_passed_through";
    assert.commandFailedWithCode(mongersDB.runCommand({
        aggregate: mongersColl.getName(),
        pipeline: pipelineForLookup,
        cursor: {},
        comment: testName
    }),
                                 51047);
    profilerHasZeroMatchingEntriesOrThrow({
        profileDB: primaryShard,
        filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
    });
    profilerHasZeroMatchingEntriesOrThrow({
        profileDB: shard1DB,
        filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
    });

    // Same test as the above with another level of nested $lookup.
    const pipelineForNestedLookup = [{
        $lookup: {
            from: mongersDB.otherColl.getName(),
            as: "field",
            pipeline: [{
                $lookup: {
                    pipeline: [{$match: {a: "val"}}, {$merge: {into: "merge_collection"}}],
                    from: mongersDB.nested.getName(),
                    as: "c",
                }
            }]
        }
    }];
    testName = "nested_lookup_with_merge_cannot_be_passed_through";
    assert.commandFailedWithCode(mongersDB.runCommand({
        aggregate: mongersColl.getName(),
        pipeline: pipelineForNestedLookup,
        cursor: {},
        comment: testName
    }),
                                 51047);
    profilerHasZeroMatchingEntriesOrThrow({
        profileDB: primaryShard,
        filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
    });
    profilerHasZeroMatchingEntriesOrThrow({
        profileDB: shard1DB,
        filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
    });

    // Test to verify that the mongerS doesn't pass the pipeline through to the primary shard when
    // one or more of $facet's sub-pipelines have one or more stages which don't allow passthrough.
    // In this sub-pipeline, the $merge stage is not allowed to pass through, which forces the
    // pipeline to be parsed on mongerS. Since $merge is not allowed within a $facet, the command
    // thus fails on mongerS without ever reaching a shard. This test-case exercises the bug
    // described in SERVER-41290.
    const pipelineForFacet = [
        {
          $facet: {
              field0: [{$match: {a: "val"}}],
              field1: [{$match: {a: "val"}}, {$merge: {into: "merge_collection"}}],
          }
        },
    ];
    testName = "facet_with_merge_cannot_be_passed_through";
    assert.commandFailedWithCode(mongersDB.runCommand({
        aggregate: mongersColl.getName(),
        pipeline: pipelineForFacet,
        cursor: {},
        comment: testName
    }),
                                 40600);
    profilerHasZeroMatchingEntriesOrThrow({
        profileDB: primaryShard,
        filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
    });
    profilerHasZeroMatchingEntriesOrThrow({
        profileDB: shard1DB,
        filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
    });

    // Same test as the above with another level of nested $facet.
    const pipelineForNestedFacet = [
        {
          $facet: {
              field0: [{$match: {a: "val"}}],
              field1: [
                  {$facet: {field2: [{$match: {a: "val"}}, {$merge: {into: "merge_collection"}}]}}
              ],
          }
        },
    ];
    testName = "facet_with_merge_cannot_be_passed_through";
    assert.commandFailedWithCode(mongersDB.runCommand({
        aggregate: mongersColl.getName(),
        pipeline: pipelineForFacet,
        cursor: {},
        comment: testName
    }),
                                 40600);
    profilerHasZeroMatchingEntriesOrThrow({
        profileDB: primaryShard,
        filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
    });
    profilerHasZeroMatchingEntriesOrThrow({
        profileDB: shard1DB,
        filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
    });

    st.stop();
})();
