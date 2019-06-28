/**
 * Tests that split aggregations whose merge pipelines are eligible to run on mongerS do so, and
 * produce the expected results. Stages which are eligible to merge on mongerS include:
 *
 * - Splitpoints whose merge components are non-blocking, e.g. $skip, $limit, $sort, $sample.
 * - Non-splittable streaming stages, e.g. $match, $project, $unwind.
 * - Blocking stages in cases where 'allowDiskUse' is false, e.g. $group, $bucketAuto.
 *
 * Because wrapping these aggregations in a $facet stage will affect how the pipeline can be merged,
 * and will therefore invalidate the results of the test cases below, we tag this test to prevent it
 * running under the 'aggregation_facet_unwind' passthrough.
 *
 * @tags: [
 *   do_not_wrap_aggregations_in_facets,
 *   requires_sharding,
 *   requires_spawning_own_processes,
 * ]
 */

(function() {
    load("jstests/libs/profiler.js");         // For profilerHas*OrThrow helper functions.
    load('jstests/libs/geo_near_random.js');  // For GeoNearRandomTest.
    load("jstests/noPassthrough/libs/server_parameter_helpers.js");  // For setParameterOnAllHosts.
    load("jstests/libs/discover_topology.js");                       // For findDataBearingNodes.

    const st = new ShardingTest({shards: 2, mongers: 1, config: 1});

    const mongersDB = st.s0.getDB(jsTestName());
    const mongersColl = mongersDB[jsTestName()];
    const unshardedColl = mongersDB[jsTestName() + "_unsharded"];

    const shard0DB = primaryShardDB = st.shard0.getDB(jsTestName());
    const shard1DB = st.shard1.getDB(jsTestName());

    assert.commandWorked(mongersDB.dropDatabase());

    // Always merge pipelines which cannot merge on mongerS on the primary shard instead, so we know
    // where to check for $mergeCursors.
    assert.commandWorked(
        mongersDB.adminCommand({setParameter: 1, internalQueryAlwaysMergeOnPrimaryShard: true}));

    // Enable sharding on the test DB and ensure its primary is shard0.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.shard0.shardName);

    // Shard the test collection on _id.
    assert.commandWorked(
        mongersDB.adminCommand({shardCollection: mongersColl.getFullName(), key: {_id: 1}}));

    // We will need to test $geoNear on this collection, so create a 2dsphere index.
    assert.commandWorked(mongersColl.createIndex({geo: "2dsphere"}));

    // We will test that $textScore metadata is not propagated to the user, so create a text index.
    assert.commandWorked(mongersColl.createIndex({text: "text"}));

    // Split the collection into 4 chunks: [MinKey, -100), [-100, 0), [0, 100), [100, MaxKey).
    assert.commandWorked(
        mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {_id: -100}}));
    assert.commandWorked(
        mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {_id: 0}}));
    assert.commandWorked(
        mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {_id: 100}}));

    // Move the [0, 100) and [100, MaxKey) chunks to shard1.
    assert.commandWorked(mongersDB.adminCommand(
        {moveChunk: mongersColl.getFullName(), find: {_id: 50}, to: st.shard1.shardName}));
    assert.commandWorked(mongersDB.adminCommand(
        {moveChunk: mongersColl.getFullName(), find: {_id: 150}, to: st.shard1.shardName}));

    // Create a random geo co-ord generator for testing.
    var georng = new GeoNearRandomTest(mongersColl);

    // Write 400 documents across the 4 chunks.
    for (let i = -200; i < 200; i++) {
        assert.writeOK(mongersColl.insert(
            {_id: i, a: [i], b: {redactThisDoc: true}, c: true, geo: georng.mkPt(), text: "txt"}));
        assert.writeOK(unshardedColl.insert({_id: i, x: i}));
    }

    let testNameHistory = new Set();

    // Clears system.profile and restarts the profiler on the primary shard. We enable profiling to
    // verify that no $mergeCursors occur during tests where we expect the merge to run on mongerS.
    function startProfiling() {
        assert.commandWorked(primaryShardDB.setProfilingLevel(0));
        primaryShardDB.system.profile.drop();
        assert.commandWorked(primaryShardDB.setProfilingLevel(2));
    }

    /**
     * Runs the aggregation specified by 'pipeline', verifying that:
     * - The number of documents returned by the aggregation matches 'expectedCount'.
     * - The merge was performed on a mongerS if 'mergeType' is 'mongers', and on a shard otherwise.
     */
    function assertMergeBehaviour(
        {testName, pipeline, mergeType, batchSize, allowDiskUse, expectedCount}) {
        // Ensure that this test has a unique name.
        assert(!testNameHistory.has(testName));
        testNameHistory.add(testName);

        // Create the aggregation options from the given arguments.
        const opts = {
            comment: testName,
            cursor: (batchSize ? {batchSize: batchSize} : {}),
        };

        if (allowDiskUse !== undefined) {
            opts.allowDiskUse = allowDiskUse;
        }

        // Verify that the explain() output's 'mergeType' field matches our expectation.
        assert.eq(
            assert.commandWorked(mongersColl.explain().aggregate(pipeline, Object.extend({}, opts)))
                .mergeType,
            mergeType);

        // Verify that the aggregation returns the expected number of results.
        assert.eq(mongersColl.aggregate(pipeline, opts).itcount(), expectedCount);

        // Verify that a $mergeCursors aggregation ran on the primary shard if 'mergeType' is not
        // 'mongers', and that no such aggregation ran otherwise.
        profilerHasNumMatchingEntriesOrThrow({
            profileDB: primaryShardDB,
            numExpectedMatches: (mergeType === "mongers" ? 0 : 1),
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: 1}
            }
        });
    }

    /**
     * Throws an assertion if the aggregation specified by 'pipeline' does not produce
     * 'expectedCount' results, or if the merge phase is not performed on the mongerS.
     */
    function assertMergeOnMongerS({testName, pipeline, batchSize, allowDiskUse, expectedCount}) {
        assertMergeBehaviour({
            testName: testName,
            pipeline: pipeline,
            mergeType: "mongers",
            batchSize: (batchSize || 10),
            allowDiskUse: allowDiskUse,
            expectedCount: expectedCount
        });
    }

    /**
     * Throws an assertion if the aggregation specified by 'pipeline' does not produce
     * 'expectedCount' results, or if the merge phase was not performed on a shard.
     */
    function assertMergeOnMongerD(
        {testName, pipeline, mergeType, batchSize, allowDiskUse, expectedCount}) {
        assertMergeBehaviour({
            testName: testName,
            pipeline: pipeline,
            mergeType: (mergeType || "anyShard"),
            batchSize: (batchSize || 10),
            allowDiskUse: allowDiskUse,
            expectedCount: expectedCount
        });
    }

    /**
     * Runs a series of test cases which will consistently merge on mongerS or mongerD regardless of
     * whether 'allowDiskUse' is true, false or omitted.
     */
    function runTestCasesWhoseMergeLocationIsConsistentRegardlessOfAllowDiskUse(allowDiskUse) {
        // Test that a $match pipeline with an empty merge stage is merged on mongerS.
        assertMergeOnMongerS({
            testName: "agg_mongers_merge_match_only",
            pipeline: [{$match: {_id: {$gte: -200, $lte: 200}}}],
            allowDiskUse: allowDiskUse,
            expectedCount: 400
        });

        // Test that a $sort stage which merges pre-sorted streams is run on mongerS.
        assertMergeOnMongerS({
            testName: "agg_mongers_merge_sort_presorted",
            pipeline: [{$match: {_id: {$gte: -200, $lte: 200}}}, {$sort: {_id: -1}}],
            allowDiskUse: allowDiskUse,
            expectedCount: 400
        });

        // Test that $skip is merged on mongerS.
        assertMergeOnMongerS({
            testName: "agg_mongers_merge_skip",
            pipeline: [{$match: {_id: {$gte: -200, $lte: 200}}}, {$sort: {_id: -1}}, {$skip: 300}],
            allowDiskUse: allowDiskUse,
            expectedCount: 100
        });

        // Test that $limit is merged on mongerS.
        assertMergeOnMongerS({
            testName: "agg_mongers_merge_limit",
            pipeline: [{$match: {_id: {$gte: -200, $lte: 200}}}, {$limit: 300}],
            allowDiskUse: allowDiskUse,
            expectedCount: 300
        });

        // Test that $sample is merged on mongerS if it is the splitpoint, since this will result in
        // a merging $sort of presorted streams in the merge pipeline.
        assertMergeOnMongerS({
            testName: "agg_mongers_merge_sample_splitpoint",
            pipeline: [{$match: {_id: {$gte: -200, $lte: 200}}}, {$sample: {size: 300}}],
            allowDiskUse: allowDiskUse,
            expectedCount: 300
        });

        // Test that $geoNear is merged on mongerS.
        assertMergeOnMongerS({
            testName: "agg_mongers_merge_geo_near",
            pipeline: [
                {$geoNear: {near: [0, 0], distanceField: "distance", spherical: true}},
                {$limit: 300}
            ],
            allowDiskUse: allowDiskUse,
            expectedCount: 300
        });

        // Test that $facet is merged on mongerS if all pipelines are mongerS-mergeable regardless of
        // 'allowDiskUse'.
        assertMergeOnMongerS({
            testName: "agg_mongers_merge_facet_all_pipes_eligible_for_mongers",
            pipeline: [
                {$match: {_id: {$gte: -200, $lte: 200}}},
                {
                  $facet: {
                      pipe1: [{$match: {_id: {$gt: 0}}}, {$skip: 10}, {$limit: 150}],
                      pipe2: [{$match: {_id: {$lt: 0}}}, {$project: {_id: 0, a: 1}}]
                  }
                }
            ],
            allowDiskUse: allowDiskUse,
            expectedCount: 1
        });

        // Test that $facet is merged on mongerD if any pipeline requires a primary shard merge,
        // regardless of 'allowDiskUse'.
        assertMergeOnMongerD({
            testName: "agg_mongers_merge_facet_pipe_needs_primary_shard_disk_use_" + allowDiskUse,
            pipeline: [
                {$match: {_id: {$gte: -200, $lte: 200}}},
                {
                  $facet: {
                      pipe1: [{$match: {_id: {$gt: 0}}}, {$skip: 10}, {$limit: 150}],
                      pipe2: [
                          {$match: {_id: {$lt: 0}}},
                          {
                            $lookup: {
                                from: unshardedColl.getName(),
                                localField: "_id",
                                foreignField: "_id",
                                as: "lookupField"
                            }
                          }
                      ]
                  }
                }
            ],
            mergeType: "primaryShard",
            allowDiskUse: allowDiskUse,
            expectedCount: 1
        });

        // Test that a pipeline whose merging half can be run on mongers using only the mongers
        // execution machinery returns the correct results.
        // TODO SERVER-30882 Find a way to assert that all stages get absorbed by mongers.
        assertMergeOnMongerS({
            testName: "agg_mongers_merge_all_mongers_runnable_skip_and_limit_stages",
            pipeline: [
                {$match: {_id: {$gte: -200, $lte: 200}}},
                {$sort: {_id: -1}},
                {$skip: 150},
                {$limit: 150},
                {$skip: 5},
                {$limit: 1},
            ],
            allowDiskUse: allowDiskUse,
            expectedCount: 1
        });

        // Test that a merge pipeline which needs to run on a shard is NOT merged on mongerS
        // regardless of 'allowDiskUse'.
        assertMergeOnMongerD({
            testName: "agg_mongers_merge_primary_shard_disk_use_" + allowDiskUse,
            pipeline: [
                {$match: {_id: {$gte: -200, $lte: 200}}},
                {$_internalSplitPipeline: {mergeType: "anyShard"}}
            ],
            mergeType: "anyShard",
            allowDiskUse: allowDiskUse,
            expectedCount: 400
        });

        // Allow sharded $lookup.
        setParameterOnAllHosts(
            DiscoverTopology.findNonConfigNodes(st.s), "internalQueryAllowShardedLookup", true);

        // Test that $lookup is merged on the primary shard when the foreign collection is
        // unsharded.
        assertMergeOnMongerD({
            testName: "agg_mongers_merge_lookup_unsharded_disk_use_" + allowDiskUse,
            pipeline: [
                {$match: {_id: {$gte: -200, $lte: 200}}},
                {
                  $lookup: {
                      from: unshardedColl.getName(),
                      localField: "_id",
                      foreignField: "_id",
                      as: "lookupField"
                  }
                }
            ],
            mergeType: "primaryShard",
            allowDiskUse: allowDiskUse,
            expectedCount: 400
        });

        // Test that $lookup is merged on mongerS when the foreign collection is sharded.
        assertMergeOnMongerS({
            testName: "agg_mongers_merge_lookup_sharded_disk_use_" + allowDiskUse,
            pipeline: [
                {$match: {_id: {$gte: -200, $lte: 200}}},
                {
                  $lookup: {
                      from: mongersColl.getName(),
                      localField: "_id",
                      foreignField: "_id",
                      as: "lookupField"
                  }
                }
            ],
            mergeType: "mongers",
            allowDiskUse: allowDiskUse,
            expectedCount: 400
        });

        // Disable sharded $lookup.
        setParameterOnAllHosts(
            DiscoverTopology.findNonConfigNodes(st.s), "internalQueryAllowShardedLookup", false);
    }

    /**
     * Runs a series of test cases which will always merge on mongerD when 'allowDiskUse' is true,
     * and on mongerS when 'allowDiskUse' is false or omitted.
     */
    function runTestCasesWhoseMergeLocationDependsOnAllowDiskUse(allowDiskUse) {
        // All test cases should merge on mongerD if allowDiskUse is true, mongerS otherwise.
        const assertMergeOnMongerX = (allowDiskUse ? assertMergeOnMongerD : assertMergeOnMongerS);

        // Test that a blocking $sort is only merged on mongerS if 'allowDiskUse' is not set.
        assertMergeOnMongerX({
            testName: "agg_mongers_merge_blocking_sort_no_disk_use",
            pipeline:
                [{$match: {_id: {$gte: -200, $lte: 200}}}, {$sort: {_id: -1}}, {$sort: {a: 1}}],
            allowDiskUse: allowDiskUse,
            expectedCount: 400
        });

        // Test that $group is only merged on mongerS if 'allowDiskUse' is not set.
        assertMergeOnMongerX({
            testName: "agg_mongers_merge_group_allow_disk_use",
            pipeline:
                [{$match: {_id: {$gte: -200, $lte: 200}}}, {$group: {_id: {$mod: ["$_id", 150]}}}],
            allowDiskUse: allowDiskUse,
            expectedCount: 299
        });

        // Test that a blocking $sample is only merged on mongerS if 'allowDiskUse' is not set.
        assertMergeOnMongerX({
            testName: "agg_mongers_merge_blocking_sample_allow_disk_use",
            pipeline: [
                {$match: {_id: {$gte: -200, $lte: 200}}},
                {$sample: {size: 300}},
                {$sample: {size: 200}}
            ],
            allowDiskUse: allowDiskUse,
            expectedCount: 200
        });

        // Test that $facet is only merged on mongerS if all pipelines are mongerS-mergeable when
        // 'allowDiskUse' is not set.
        assertMergeOnMongerX({
            testName: "agg_mongers_merge_facet_allow_disk_use",
            pipeline: [
                {$match: {_id: {$gte: -200, $lte: 200}}},
                {
                  $facet: {
                      pipe1: [{$match: {_id: {$gt: 0}}}, {$skip: 10}, {$limit: 150}],
                      pipe2: [{$match: {_id: {$lt: 0}}}, {$sort: {a: -1}}]
                  }
                }
            ],
            allowDiskUse: allowDiskUse,
            expectedCount: 1
        });

        // Test that $bucketAuto is only merged on mongerS if 'allowDiskUse' is not set.
        assertMergeOnMongerX({
            testName: "agg_mongers_merge_bucket_auto_allow_disk_use",
            pipeline: [
                {$match: {_id: {$gte: -200, $lte: 200}}},
                {$bucketAuto: {groupBy: "$_id", buckets: 10}}
            ],
            allowDiskUse: allowDiskUse,
            expectedCount: 10
        });

        //
        // Test composite stages.
        //

        // Test that $bucket ($group->$sort) is merged on mongerS iff 'allowDiskUse' is not set.
        assertMergeOnMongerX({
            testName: "agg_mongers_merge_bucket_allow_disk_use",
            pipeline: [
                {$match: {_id: {$gte: -200, $lte: 200}}},
                {
                  $bucket: {
                      groupBy: "$_id",
                      boundaries: [-200, -150, -100, -50, 0, 50, 100, 150, 200]
                  }
                }
            ],
            allowDiskUse: allowDiskUse,
            expectedCount: 8
        });

        // Test that $sortByCount ($group->$sort) is merged on mongerS iff 'allowDiskUse' isn't set.
        assertMergeOnMongerX({
            testName: "agg_mongers_merge_sort_by_count_allow_disk_use",
            pipeline:
                [{$match: {_id: {$gte: -200, $lte: 200}}}, {$sortByCount: {$mod: ["$_id", 150]}}],
            allowDiskUse: allowDiskUse,
            expectedCount: 299
        });

        // Test that $count ($group->$project) is merged on mongerS iff 'allowDiskUse' is not set.
        assertMergeOnMongerX({
            testName: "agg_mongers_merge_count_allow_disk_use",
            pipeline: [{$match: {_id: {$gte: -150, $lte: 1500}}}, {$count: "doc_count"}],
            allowDiskUse: allowDiskUse,
            expectedCount: 1
        });
    }

    // Run all test cases for each potential value of 'allowDiskUse'.
    for (let allowDiskUse of[false, undefined, true]) {
        // Reset the profiler and clear the list of tests that ran on the previous iteration.
        testNameHistory.clear();
        startProfiling();

        // Run all test cases.
        runTestCasesWhoseMergeLocationIsConsistentRegardlessOfAllowDiskUse(allowDiskUse);
        runTestCasesWhoseMergeLocationDependsOnAllowDiskUse(allowDiskUse);
    }

    // Start a new profiling session before running the final few tests.
    startProfiling();

    // Test that merge pipelines containing all mongers-runnable stages produce the expected output.
    assertMergeOnMongerS({
        testName: "agg_mongers_merge_all_mongers_runnable_stages",
        pipeline: [
            {$geoNear: {near: [0, 0], distanceField: "distance", spherical: true}},
            {$sort: {a: 1}},
            {$skip: 150},
            {$limit: 150},
            {$addFields: {d: true}},
            {$unwind: "$a"},
            {$sample: {size: 100}},
            {$project: {c: 0, geo: 0, distance: 0}},
            {$group: {_id: "$_id", doc: {$push: "$$CURRENT"}}},
            {$unwind: "$doc"},
            {$replaceRoot: {newRoot: "$doc"}},
            {$facet: {facetPipe: [{$match: {_id: {$gte: -200, $lte: 200}}}]}},
            {$unwind: "$facetPipe"},
            {$replaceRoot: {newRoot: "$facetPipe"}},
            {
              $redact: {
                  $cond:
                      {if: {$eq: ["$redactThisDoc", true]}, then: "$$PRUNE", else: "$$DESCEND"}
              }
            },
            {
              $match: {
                  _id: {$gte: -50, $lte: 100},
                  a: {$type: "number", $gte: -50, $lte: 100},
                  b: {$exists: false},
                  c: {$exists: false},
                  d: true,
                  geo: {$exists: false},
                  distance: {$exists: false},
                  text: "txt"
              }
            }
        ],
        expectedCount: 100
    });

    // Test that metadata is not propagated to the user when a pipeline which produces metadata
    // fields merges on mongerS.
    const metaDataTests = [
        {pipeline: [{$sort: {_id: -1}}], verifyNoMetaData: (doc) => assert.isnull(doc.$sortKey)},
        {
          pipeline: [{$match: {$text: {$search: "txt"}}}],
          verifyNoMetaData: (doc) => assert.isnull(doc.$textScore)
        },
        {
          pipeline: [{$sample: {size: 300}}],
          verifyNoMetaData: (doc) => assert.isnull(doc.$randVal)
        },
        {
          pipeline: [{$match: {$text: {$search: "txt"}}}, {$sort: {text: 1}}],
          verifyNoMetaData:
              (doc) => assert.docEq([doc.$textScore, doc.$sortKey], [undefined, undefined])
        }
    ];

    for (let metaDataTest of metaDataTests) {
        assert.gte(mongersColl.aggregate(metaDataTest.pipeline).itcount(), 300);
        mongersColl.aggregate(metaDataTest.pipeline).forEach(metaDataTest.verifyNoMetaData);
    }

    st.stop();
})();
