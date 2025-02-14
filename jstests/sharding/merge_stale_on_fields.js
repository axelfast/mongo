// Tests that an $merge stage is able to default the "on" fields to the correct value - even if one
// or more of the involved nodes has a stale cache of the routing information.
(function() {
    "use strict";

    load("jstests/aggregation/extras/merge_helpers.js");  // For withEachMergeMode,

    const st = new ShardingTest({shards: 2, mongers: 2});

    const dbName = "merge_stale_unique_key";
    assert.commandWorked(st.s.adminCommand({enableSharding: dbName}));

    const source = st.s0.getDB(dbName).source;
    const target = st.s0.getDB(dbName).target;

    // Test that an $merge through a stale mongers can still use the correct "on" fields and succeed.
    (function testDefaultOnFieldsIsRecent() {
        const freshMongers = st.s0;
        const staleMongers = st.s1;

        // Set up two collections for an aggregate with an $merge: The source collection will be
        // unsharded and the target collection will be sharded amongst the two shards.
        const staleMongersDB = staleMongers.getDB(dbName);
        st.shardColl(source, {_id: 1}, {_id: 0}, {_id: 1});

        (function setupStaleMongers() {
            // Shard the collection through 'staleMongers', setting up 'staleMongers' to believe the
            // collection is sharded by {sk: 1, _id: 1}.
            assert.commandWorked(staleMongersDB.adminCommand(
                {shardCollection: target.getFullName(), key: {sk: 1, _id: 1}}));
            // Perform a query through that mongers to ensure the cache is populated.
            assert.eq(0, staleMongersDB[target.getName()].find().itcount());

            // Drop the collection from the other mongers - it is no longer sharded but the stale
            // mongers doesn't know that yet.
            target.drop();
        }());

        // At this point 'staleMongers' will believe that the target collection is sharded. This
        // should not prevent it from running an $merge without "on" fields specified.
        // Specifically, the mongers should force a refresh of its cache before defaulting the "on"
        // fields.
        assert.commandWorked(source.insert({_id: 'seed'}));

        // If we had used the stale "on" fields, this aggregation would fail since the documents do
        // not have an 'sk' field.
        assert.doesNotThrow(() => staleMongersDB[source.getName()].aggregate([
            {$merge: {into: target.getName(), whenMatched: 'fail', whenNotMatched: 'insert'}}
        ]));
        assert.eq(target.find().toArray(), [{_id: 'seed'}]);
        target.drop();
    }());

    // Test that if the collection is dropped and re-sharded during the course of the aggregation
    // that the operation will fail rather than proceed with the old shard key.
    function testEpochChangeDuringAgg({mergeSpec, failpoint, failpointData}) {
        // Converts a single string or an array of strings into it's object spec form. For instance,
        // for input ["a", "b"] the returned object would be {a: 1, b: 1}.
        function indexSpecFromOnFields(onFields) {
            let spec = {};
            if (typeof(onFields) == "string") {
                spec[onFields] = 1;
            } else {
                onFields.forEach((field) => {
                    spec[field] = 1;
                });
            }
            return spec;
        }

        target.drop();
        if (mergeSpec.hasOwnProperty('on')) {
            assert.commandWorked(
                target.createIndex(indexSpecFromOnFields(mergeSpec.on), {unique: true}));
            assert.commandWorked(st.s.adminCommand(
                {shardCollection: target.getFullName(), key: indexSpecFromOnFields(mergeSpec.on)}));
        } else {
            assert.commandWorked(
                st.s.adminCommand({shardCollection: target.getFullName(), key: {sk: 1, _id: 1}}));
        }

        // Use a failpoint to make the query feeding into the aggregate hang while we drop the
        // collection.
        [st.rs0.getPrimary(), st.rs1.getPrimary()].forEach((mongerd) => {
            assert.commandWorked(mongerd.adminCommand(
                {configureFailPoint: failpoint, mode: "alwaysOn", data: failpointData || {}}));
        });
        let parallelShellJoiner;
        try {
            let parallelCode = `
                const source = db.getSiblingDB("${dbName}").${source.getName()};
                const error = assert.throws(() => source.aggregate([
                    {$addFields: {sk: "$_id"}},
                    {$merge: ${tojsononeline(mergeSpec)}}
                ]));
                assert.eq(error.code, ErrorCodes.StaleEpoch);
            `;

            if (mergeSpec.hasOwnProperty("on")) {
                // If a user specifies their own "on" fields, we don't need to fail an aggregation
                // if the collection is dropped and recreated or the epoch otherwise changes. We are
                // allowed to fail such an operation should we choose to in the future, but for now
                // we don't expect to because we do not do anything special on mongers to ensure the
                // catalog cache is up to date, so do not want to attach mongers's believed epoch to
                // the command for the shards.
                parallelCode = `
                    const source = db.getSiblingDB("${dbName}").${source.getName()};
                    assert.doesNotThrow(() => source.aggregate([
                        {$addFields: {sk: "$_id"}},
                        {$merge: ${tojsononeline(mergeSpec)}}
                    ]));
                `;
            }

            parallelShellJoiner = startParallelShell(parallelCode, st.s.port);

            // Wait for the merging $merge to appear in the currentOp output from the shards. We
            // should see that the $merge stage has an 'epoch' field serialized from the mongers.
            const getAggOps = function() {
                return st.s.getDB("admin")
                    .aggregate([
                        {$currentOp: {}},
                        {$match: {"cursor.originatingCommand.pipeline": {$exists: true}}}
                    ])
                    .toArray();
            };
            const hasMergeRunning = function() {
                return getAggOps()
                           .filter((op) => {
                               const pipeline = op.cursor.originatingCommand.pipeline;
                               return pipeline.length > 0 &&
                                   pipeline[pipeline.length - 1].hasOwnProperty("$merge");
                           })
                           .length >= 1;
            };
            assert.soon(hasMergeRunning, () => tojson(getAggOps()));

            // Drop the collection so that the epoch changes.
            target.drop();
        } finally {
            [st.rs0.getPrimary(), st.rs1.getPrimary()].forEach((mongerd) => {
                assert.commandWorked(
                    mongerd.adminCommand({configureFailPoint: failpoint, mode: "off"}));
            });
        }
        parallelShellJoiner();
    }

    // Insert enough documents to force a yield.
    const bulk = source.initializeUnorderedBulkOp();
    for (let i = 0; i < 1000; ++i) {
        bulk.insert({_id: i});
    }
    assert.commandWorked(bulk.execute());

    withEachMergeMode(({whenMatchedMode, whenNotMatchedMode}) => {
        // Skip the combination of merge modes which will fail depending on the contents of the
        // source and target collection, as this will cause a different assertion error from the one
        // expected.
        if (whenNotMatchedMode == "fail")
            return;

        testEpochChangeDuringAgg({
            mergeSpec: {
                into: target.getName(),
                whenMatched: whenMatchedMode,
                whenNotMatched: whenNotMatchedMode
            },
            failpoint: "setYieldAllLocksHang",
            failpointData: {namespace: source.getFullName()}
        });
        testEpochChangeDuringAgg({
            mergeSpec: {
                into: target.getName(),
                whenMatched: whenMatchedMode,
                whenNotMatched: whenNotMatchedMode,
                on: "sk"
            },
            failpoint: "setYieldAllLocksHang",
            failpointData: {namespace: source.getFullName()}
        });

    });
    // Test with some different failpoints to prove we will detect an epoch change in the middle
    // of the inserts or updates.
    testEpochChangeDuringAgg({
        mergeSpec: {into: target.getName(), whenMatched: "fail", whenNotMatched: "insert"},
        failpoint: "hangDuringBatchInsert",
        failpointData: {nss: target.getFullName()}
    });
    testEpochChangeDuringAgg({
        mergeSpec: {into: target.getName(), whenMatched: "replace", whenNotMatched: "insert"},
        failpoint: "hangDuringBatchUpdate",
        failpointData: {nss: target.getFullName()}
    });

    st.stop();
}());
