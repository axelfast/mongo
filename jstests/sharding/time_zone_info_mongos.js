// Test that mongerS accepts --timeZoneInfo <timezoneDBPath> as a command-line argument and that an
// aggregation pipeline with timezone expressions executes correctly on mongerS.
(function() {
    const tzGoodInfo = "jstests/libs/config_files/good_timezone_info";
    const tzBadInfo = "jstests/libs/config_files/bad_timezone_info";
    const tzNoInfo = "jstests/libs/config_files/missing_directory";

    const st = new ShardingTest({
        shards: 2,
        mongers: {s0: {timeZoneInfo: tzGoodInfo}},
        rs: {nodes: 1, timeZoneInfo: tzGoodInfo}
    });

    const mongersDB = st.s0.getDB(jsTestName());
    const mongersColl = mongersDB[jsTestName()];

    assert.commandWorked(mongersDB.dropDatabase());

    // Confirm that the timeZoneInfo command-line argument has been set on mongerS.
    const mongersCfg = assert.commandWorked(mongersDB.adminCommand({getCmdLineOpts: 1}));
    assert.eq(mongersCfg.parsed.processManagement.timeZoneInfo, tzGoodInfo);

    // Test that a bad timezone file causes mongerS startup to fail.
    let conn = MongoRunner.runMongos({configdb: st.configRS.getURL(), timeZoneInfo: tzBadInfo});
    assert.eq(conn, null, "expected launching mongers with bad timezone rules to fail");
    assert.neq(-1, rawMongoProgramOutput().indexOf("Fatal assertion 40475"));

    // Test that a non-existent timezone directory causes mongerS startup to fail.
    conn = MongoRunner.runMongos({configdb: st.configRS.getURL(), timeZoneInfo: tzNoInfo});
    assert.eq(conn, null, "expected launching mongers with bad timezone rules to fail");
    // Look for either old or new error message
    assert(rawMongoProgramOutput().indexOf("Failed to create service context") != -1 ||
           rawMongoProgramOutput().indexOf("Failed global initialization") != -1);

    // Enable sharding on the test DB and ensure its primary is st.shard0.shardName.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.rs0.getURL());

    // Shard the test collection on _id.
    assert.commandWorked(
        mongersDB.adminCommand({shardCollection: mongersColl.getFullName(), key: {_id: 1}}));

    // Split the collection into 2 chunks: [MinKey, 0), [0, MaxKey).
    assert.commandWorked(
        mongersDB.adminCommand({split: mongersColl.getFullName(), middle: {_id: 0}}));

    // Move the [0, MaxKey) chunk to st.shard1.shardName.
    assert.commandWorked(mongersDB.adminCommand(
        {moveChunk: mongersColl.getFullName(), find: {_id: 1}, to: st.rs1.getURL()}));

    // Write a document containing a 'date' field to each chunk.
    assert.writeOK(mongersColl.insert({_id: -1, date: ISODate("2017-11-13T12:00:00.000+0000")}));
    assert.writeOK(mongersColl.insert({_id: 1, date: ISODate("2017-11-13T03:00:00.000+0600")}));

    // Constructs a pipeline which splits the 'date' field into its constituent parts on mongerD,
    // reassembles the original date on mongerS, and verifies that the two match. All timezone
    // expressions in the pipeline use the passed 'tz' string or, if absent, default to "GMT".
    function buildTimeZonePipeline(tz) {
        // We use $const here so that the input pipeline matches the format of the explain output.
        const tzExpr = {$const: (tz || "GMT")};
        return [
            {$addFields: {mongerdParts: {$dateToParts: {date: "$date", timezone: tzExpr}}}},
            {$_internalSplitPipeline: {mergeType: "mongers"}},
            {
              $addFields: {
                  mongersDate: {
                      $dateFromParts: {
                          year: "$mongerdParts.year",
                          month: "$mongerdParts.month",
                          day: "$mongerdParts.day",
                          hour: "$mongerdParts.hour",
                          minute: "$mongerdParts.minute",
                          second: "$mongerdParts.second",
                          millisecond: "$mongerdParts.millisecond",
                          timezone: tzExpr
                      }
                  }
              }
            },
            {$match: {$expr: {$eq: ["$date", "$mongersDate"]}}}
        ];
    }

    // Confirm that the pipe splits at '$_internalSplitPipeline' and that the merge runs on mongerS.
    let timeZonePipeline = buildTimeZonePipeline("GMT");
    const tzExplain = assert.commandWorked(mongersColl.explain().aggregate(timeZonePipeline));
    assert.eq(tzExplain.splitPipeline.shardsPart, [timeZonePipeline[0]]);
    assert.eq(tzExplain.splitPipeline.mergerPart, timeZonePipeline.slice(1));
    assert.eq(tzExplain.mergeType, "mongers");

    // Confirm that both documents are output by the pipeline, demonstrating that the date has been
    // correctly disassembled on mongerD and reassembled on mongerS.
    assert.eq(mongersColl.aggregate(timeZonePipeline).itcount(), 2);

    // Confirm that aggregating with a timezone which is not present in 'good_timezone_info' fails.
    timeZonePipeline = buildTimeZonePipeline("Europe/Dublin");
    assert.eq(assert.throws(() => mongersColl.aggregate(timeZonePipeline)).code, 40485);

    st.stop();
})();