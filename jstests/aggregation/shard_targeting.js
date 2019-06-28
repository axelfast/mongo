/**
 * Test that aggregations are sent directly to a single shard in the case where the data required by
 * the pipeline's initial query all resides on that shard, and that we correctly back out and
 * re-target in the event that a stale config exception is received.
 *
 * In particular:
 *
 * - If the data required by the aggregation all resides on a single shard (including multi-chunk
 * range $matches), send the entire pipeline to that shard and do not perform a $mergeCursors.
 * - In the case of a stage which requires a primary shard merge, do not split the pipeline or
 * generate a $mergeCursors if the data required by the aggregation all resides on the primary
 * shard.
 *
 * Because wrapping these aggregations in a $facet stage will affect how the pipeline is targeted,
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
    load("jstests/libs/profiler.js");  // For profilerHas*OrThrow helper functions.

    const st = new ShardingTest({shards: 2, mongers: 2, config: 1});

    // mongersForAgg will be used to perform all aggregations.
    // mongersForMove does all chunk migrations, leaving mongersForAgg with stale config metadata.
    const mongersForAgg = st.s0;
    const mongersForMove = st.s1;

    const mongersDB = mongersForAgg.getDB(jsTestName());
    const mongersColl = mongersDB.test;

    const shard0DB = primaryShardDB = st.shard0.getDB(jsTestName());
    const shard1DB = st.shard1.getDB(jsTestName());

    // Turn off best-effort recipient metadata refresh post-migration commit on both shards because
    // it creates non-determinism for the profiler.
    assert.commandWorked(st.shard0.getDB('admin').runCommand(
        {configureFailPoint: 'doNotRefreshRecipientAfterCommit', mode: 'alwaysOn'}));
    assert.commandWorked(st.shard1.getDB('admin').runCommand(
        {configureFailPoint: 'doNotRefreshRecipientAfterCommit', mode: 'alwaysOn'}));

    // Turn off automatic shard refresh in mongers when a stale config error is thrown.
    assert.commandWorked(mongersForAgg.getDB('admin').runCommand(
        {configureFailPoint: 'doNotRefreshShardsOnRetargettingError', mode: 'alwaysOn'}));

    assert.commandWorked(mongersDB.dropDatabase());

    // Enable sharding on the test DB and ensure its primary is st.shard0.shardName.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.shard0.shardName);

    // Shard the test collection on _id.
    assert.commandWorked(
        mongersDB.adminCommand({shardCollection: mongersColl.getFullName(), key: {_id: 1}}));

    // Split the collection into 4 chunks: [MinKey, -100), [-100, 0), [0, 100), [100, MaxKey).
    assert.commandWorked(
        mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {_id: -100}}));
    assert.commandWorked(
        mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {_id: 0}}));
    assert.commandWorked(
        mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {_id: 100}}));

    // Move the [0, 100) and [100, MaxKey) chunks to st.shard1.shardName.
    assert.commandWorked(mongersDB.adminCommand(
        {moveChunk: mongersColl.getFullName(), find: {_id: 50}, to: st.shard1.shardName}));
    assert.commandWorked(mongersDB.adminCommand(
        {moveChunk: mongersColl.getFullName(), find: {_id: 150}, to: st.shard1.shardName}));

    // Write one document into each of the chunks.
    assert.writeOK(mongersColl.insert({_id: -150}));
    assert.writeOK(mongersColl.insert({_id: -50}));
    assert.writeOK(mongersColl.insert({_id: 50}));
    assert.writeOK(mongersColl.insert({_id: 150}));

    const shardExceptions =
        [ErrorCodes.StaleConfig, ErrorCodes.StaleShardVersion, ErrorCodes.StaleEpoch];

    // Create an $_internalSplitPipeline stage that forces the merge to occur on the Primary shard.
    const forcePrimaryMerge = [{$_internalSplitPipeline: {mergeType: "primaryShard"}}];

    function runAggShardTargetTest({splitPoint}) {
        // Ensure that both mongerS have up-to-date caches, and enable the profiler on both shards.
        assert.commandWorked(mongersForAgg.getDB("admin").runCommand({flushRouterConfig: 1}));
        assert.commandWorked(mongersForMove.getDB("admin").runCommand({flushRouterConfig: 1}));

        assert.commandWorked(shard0DB.setProfilingLevel(2));
        assert.commandWorked(shard1DB.setProfilingLevel(2));

        //
        // Test cases.
        //

        let testName, outColl;

        // Test that a range query is passed through if the chunks encompassed by the query all lie
        // on a single shard, in this case st.shard0.shardName.
        testName = "agg_shard_targeting_range_single_shard_all_chunks_on_same_shard";
        assert.eq(mongersColl
                      .aggregate([{$match: {_id: {$gte: -150, $lte: -50}}}].concat(splitPoint),
                                 {comment: testName})
                      .itcount(),
                  2);

        // We expect one aggregation on shard0, none on shard1, and no $mergeCursors on shard0 (the
        // primary shard).
        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shard0DB,
            filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
        });
        profilerHasZeroMatchingEntriesOrThrow({
            profileDB: shard1DB,
            filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
        });
        profilerHasZeroMatchingEntriesOrThrow({
            profileDB: primaryShardDB,
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: 1}
            }
        });

        // Test that a range query with a stage that requires a primary shard merge ($out in this
        // case) is passed through if the chunk ranges encompassed by the query all lie on the
        // primary shard.
        testName = "agg_shard_targeting_range_all_chunks_on_primary_shard_out_no_merge";
        outColl = mongersDB[testName];

        assert.commandWorked(mongersDB.runCommand({
            aggregate: mongersColl.getName(),
            pipeline: [{$match: {_id: {$gte: -150, $lte: -50}}}].concat(splitPoint).concat([
                {$out: testName}
            ]),
            comment: testName,
            cursor: {}
        }));

        // We expect one aggregation on shard0, none on shard1, and no $mergeCursors on shard0 (the
        // primary shard).
        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shard0DB,
            filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
        });
        profilerHasZeroMatchingEntriesOrThrow({
            profileDB: shard1DB,
            filter: {"command.aggregate": mongersColl.getName(), "command.comment": testName}
        });
        profilerHasZeroMatchingEntriesOrThrow({
            profileDB: primaryShardDB,
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: 1}
            }
        });

        // Verify that the contents of the $out collection are as expected.
        assert.eq(outColl.find().sort({_id: 1}).toArray(), [{_id: -150}, {_id: -50}]);

        // Test that a passthrough will back out and split the pipeline if we try to target a single
        // shard, get a stale config exception, and find that more than one shard is now involved.
        // Move the _id: [-100, 0) chunk from st.shard0.shardName to st.shard1.shardName via
        // mongersForMove.
        assert.commandWorked(mongersForMove.getDB("admin").runCommand({
            moveChunk: mongersColl.getFullName(),
            find: {_id: -50},
            to: st.shard1.shardName,
        }));

        // Run the same aggregation that targeted a single shard via the now-stale mongerS. It should
        // attempt to send the aggregation to st.shard0.shardName, hit a stale config exception,
        // split the pipeline and redispatch. We append an $_internalSplitPipeline stage in order to
        // force a shard merge rather than a mongerS merge.
        testName = "agg_shard_targeting_backout_passthrough_and_split_if_cache_is_stale";
        assert.eq(mongersColl
                      .aggregate([{$match: {_id: {$gte: -150, $lte: -50}}}]
                                     .concat(splitPoint)
                                     .concat(forcePrimaryMerge),
                                 {comment: testName})
                      .itcount(),
                  2);

        // Before the first dispatch:
        // - mongersForMove and st.shard0.shardName (the donor shard) are up to date.
        // - mongersForAgg and st.shard1.shardName are stale. mongersForAgg incorrectly believes that
        //   the necessary data is all on st.shard0.shardName.
        //
        // We therefore expect that:
        // - mongersForAgg will throw a stale config error when it attempts to establish a
        // single-shard cursor on st.shard0.shardName (attempt 1).
        // - mongersForAgg will back out, refresh itself, and redispatch to both shards.
        // - st.shard1.shardName will throw a stale config and refresh itself when the split
        // pipeline is sent to it (attempt 2).
        // - mongersForAgg will back out and redispatch (attempt 3).
        // - The aggregation will succeed on the third dispatch.

        // We confirm this behaviour via the following profiler results:

        // - One aggregation on st.shard0.shardName with a shard version exception (indicating that
        // the mongerS was stale).
        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shard0DB,
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: false},
                errCode: {$in: shardExceptions}
            }
        });

        // - One aggregation on st.shard1.shardName with a shard version exception (indicating that
        // the shard was stale).
        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shard1DB,
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: false},
                errCode: {$in: shardExceptions}
            }
        });

        // - At most two aggregations on st.shard0.shardName with no stale config exceptions. The
        // first, if present, is an aborted cursor created if the command reaches
        // st.shard0.shardName before st.shard1.shardName throws its stale config exception during
        // attempt 2. The second profiler entry is from the aggregation which succeeded.
        profilerHasAtLeastOneAtMostNumMatchingEntriesOrThrow({
            profileDB: shard0DB,
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: false},
                errCode: {$exists: false}
            },
            maxExpectedMatches: 2
        });

        // - One aggregation on st.shard1.shardName with no stale config exception.
        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shard1DB,
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: false},
                errCode: {$exists: false}
            }
        });

        // - One $mergeCursors aggregation on primary st.shard0.shardName, since we eventually
        // target both shards after backing out the passthrough and splitting the pipeline.
        profilerHasSingleMatchingEntryOrThrow({
            profileDB: primaryShardDB,
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: true}
            }
        });

        // Move the _id: [-100, 0) chunk back from st.shard1.shardName to st.shard0.shardName via
        // mongersForMove. Shard0 and mongersForAgg are now stale.
        assert.commandWorked(mongersForMove.getDB("admin").runCommand({
            moveChunk: mongersColl.getFullName(),
            find: {_id: -50},
            to: st.shard0.shardName,
            _waitForDelete: true
        }));

        // Run the same aggregation via the now-stale mongerS. It should split the pipeline, hit a
        // stale config exception, and reset to the original single-shard pipeline upon refresh. We
        // append an $_internalSplitPipeline stage in order to force a shard merge rather than a
        // mongerS merge.
        testName = "agg_shard_targeting_backout_split_pipeline_and_reassemble_if_cache_is_stale";
        assert.eq(mongersColl
                      .aggregate([{$match: {_id: {$gte: -150, $lte: -50}}}]
                                     .concat(splitPoint)
                                     .concat(forcePrimaryMerge),
                                 {comment: testName})
                      .itcount(),
                  2);

        // Before the first dispatch:
        // - mongersForMove and st.shard1.shardName (the donor shard) are up to date.
        // - mongersForAgg and st.shard0.shardName are stale. mongersForAgg incorrectly believes that
        // the necessary data is spread across both shards.
        //
        // We therefore expect that:
        // - mongersForAgg will throw a stale config error when it attempts to establish a cursor on
        // st.shard1.shardName (attempt 1).
        // - mongersForAgg will back out, refresh itself, and redispatch to st.shard0.shardName.
        // - st.shard0.shardName will throw a stale config and refresh itself when the pipeline is
        // sent to it (attempt 2).
        // - mongersForAgg will back out, and redispatch (attempt 3).
        // - The aggregation will succeed on the third dispatch.

        // We confirm this behaviour via the following profiler results:

        // - One aggregation on st.shard1.shardName with a shard version exception (indicating that
        // the mongerS was stale).
        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shard1DB,
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: false},
                errCode: {$in: shardExceptions}
            }
        });

        // - One aggregation on st.shard0.shardName with a shard version exception (indicating that
        // the shard was stale).
        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shard0DB,
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: false},
                errCode: {$in: shardExceptions}
            }
        });

        // - At most two aggregations on st.shard0.shardName with no stale config exceptions. The
        // first, if present, is an aborted cursor created if the command reaches
        // st.shard0.shardName before st.shard1.shardName throws its stale config exception during
        // attempt 1. The second profiler entry is the aggregation which succeeded.
        profilerHasAtLeastOneAtMostNumMatchingEntriesOrThrow({
            profileDB: shard0DB,
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: false},
                errCode: {$exists: false}
            },
            maxExpectedMatches: 2
        });

        // No $mergeCursors aggregation on primary st.shard0.shardName, since after backing out the
        // split pipeline we eventually target only st.shard0.shardName.
        profilerHasZeroMatchingEntriesOrThrow({
            profileDB: primaryShardDB,
            filter: {
                "command.aggregate": mongersColl.getName(),
                "command.comment": testName,
                "command.pipeline.$mergeCursors": {$exists: true}
            }
        });

        // Clean up the test run by dropping the $out collection and resetting the profiler.
        assert(outColl.drop());

        assert.commandWorked(shard0DB.setProfilingLevel(0));
        assert.commandWorked(shard1DB.setProfilingLevel(0));

        assert(shard0DB.system.profile.drop());
        assert(shard1DB.system.profile.drop());
    }

    // Run tests with a variety of splitpoints, testing the pipeline split and re-assembly logic in
    // cases where the merge pipeline is empty, where the split stage is moved from shard to merge
    // pipe ($facet, $lookup), and where there are both shard and merge versions of the split source
    // ($sort, $group, $limit). Each test case will ultimately produce the same output.
    runAggShardTargetTest({splitPoint: []});
    runAggShardTargetTest({splitPoint: [{$sort: {_id: 1}}]});
    runAggShardTargetTest({splitPoint: [{$group: {_id: "$_id"}}]});
    runAggShardTargetTest({splitPoint: [{$limit: 4}]});
    runAggShardTargetTest({
        splitPoint: [
            {$facet: {facetPipe: [{$match: {_id: {$gt: MinKey}}}]}},
            {$unwind: "$facetPipe"},
            {$replaceRoot: {newRoot: "$facetPipe"}}
        ]
    });
    runAggShardTargetTest({
        splitPoint: [
            {
              $lookup: {
                  from: "dummycoll",
                  localField: "dummyfield",
                  foreignField: "dummyfield",
                  as: "lookupRes"
              }
            },
            {$project: {lookupRes: 0}}
        ]
    });

    st.stop();
})();
