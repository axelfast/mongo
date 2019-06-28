/**
 * Tests that query options are not dropped by mongers when a query against a view is rewritten as an
 * aggregation against the underlying collection.
 */
(function() {
    "use strict";

    load("jstests/libs/profiler.js");  // For profilerHasSingleMatchingEntryOrThrow.

    const st = new ShardingTest({
        name: "view_rewrite",
        shards: 2,
        other: {
            rs0: {
                nodes: [
                    {rsConfig: {priority: 1}},
                    {rsConfig: {priority: 0, tags: {"tag": "secondary"}}}
                ]
            },
            rs1: {
                nodes: [
                    {rsConfig: {priority: 1}},
                    {rsConfig: {priority: 0, tags: {"tag": "secondary"}}}
                ]
            },
            enableBalancer: false
        }
    });

    const mongers = st.s0;
    const config = mongers.getDB("config");
    const mongersDB = mongers.getDB("view_rewrite");
    const coll = mongersDB.getCollection("coll");

    assert.commandWorked(config.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), "view_rewrite-rs0");

    const rs0Secondary = st.rs0.getSecondary();
    const rs1Primary = st.rs1.getPrimary();
    const rs1Secondary = st.rs1.getSecondary();

    assert.commandWorked(config.adminCommand({shardCollection: coll.getFullName(), key: {a: 1}}));
    assert.commandWorked(mongers.adminCommand({split: coll.getFullName(), middle: {a: 5}}));
    assert.commandWorked(mongersDB.adminCommand(
        {moveChunk: coll.getFullName(), find: {a: 5}, to: "view_rewrite-rs1"}));

    for (let i = 0; i < 10; ++i) {
        assert.writeOK(coll.insert({a: i}));
    }

    assert.commandWorked(mongersDB.createView("view", coll.getName(), []));
    const view = mongersDB.getCollection("view");

    //
    // Confirms that queries run against views on mongers result in execution of a rewritten
    // aggregation that contains all expected query options.
    //
    function confirmOptionsInProfiler(shardPrimary) {
        assert.commandWorked(shardPrimary.setProfilingLevel(2));

        // Aggregation
        assert.commandWorked(mongersDB.runCommand({
            aggregate: "view",
            pipeline: [],
            comment: "agg_rewrite",
            maxTimeMS: 5 * 60 * 1000,
            readConcern: {level: "linearizable"},
            cursor: {}
        }));

        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shardPrimary,
            filter: {
                "ns": coll.getFullName(),
                "command.aggregate": coll.getName(),
                "command.comment": "agg_rewrite",
                "command.maxTimeMS": {"$exists": true},
                "command.readConcern": {level: "linearizable"},
                "command.pipeline.$mergeCursors": {"$exists": false},
                "nreturned": {"$exists": true}
            }
        });

        // Find
        assert.commandWorked(mongersDB.runCommand({
            find: "view",
            comment: "find_rewrite",
            maxTimeMS: 5 * 60 * 1000,
            readConcern: {level: "linearizable"}
        }));

        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shardPrimary,
            filter: {
                "ns": coll.getFullName(),
                "command.aggregate": coll.getName(),
                "command.comment": "find_rewrite",
                "command.maxTimeMS": {"$exists": true},
                "command.readConcern": {level: "linearizable"},
                "command.pipeline.$mergeCursors": {"$exists": false},
                "nreturned": {"$exists": true}
            }
        });

        // Count
        assert.commandWorked(mongersDB.runCommand({
            count: "view",
            comment: "count_rewrite",
            maxTimeMS: 5 * 60 * 1000,
            readConcern: {level: "linearizable"}
        }));

        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shardPrimary,
            filter: {
                "ns": coll.getFullName(),
                "command.aggregate": coll.getName(),
                "command.comment": "count_rewrite",
                "command.maxTimeMS": {"$exists": true},
                "command.readConcern": {level: "linearizable"},
                "command.pipeline.$mergeCursors": {"$exists": false},
                "nreturned": {"$exists": true}
            }
        });

        // Distinct
        assert.commandWorked(mongersDB.runCommand({
            distinct: "view",
            key: "a",
            comment: "distinct_rewrite",
            maxTimeMS: 5 * 60 * 1000,
            readConcern: {level: "linearizable"}
        }));

        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shardPrimary,
            filter: {
                "ns": coll.getFullName(),
                "command.aggregate": coll.getName(),
                "command.comment": "distinct_rewrite",
                "command.maxTimeMS": {"$exists": true},
                "command.readConcern": {level: "linearizable"},
                "command.pipeline.$mergeCursors": {"$exists": false},
                "nreturned": {"$exists": true}
            }
        });

        assert.commandWorked(shardPrimary.setProfilingLevel(0));
        shardPrimary.system.profile.drop();
    }

    //
    // Confirms that queries run against views on mongers are executed against a tagged secondary, as
    // per readPreference setting.
    //
    function confirmReadPreference(shardSecondary) {
        assert.commandWorked(shardSecondary.setProfilingLevel(2));

        // Aggregation
        assert.commandWorked(mongersDB.runCommand({
            query: {aggregate: "view", pipeline: [], comment: "agg_readPref", cursor: {}},
            $readPreference: {mode: "nearest", tags: [{tag: "secondary"}]},
            readConcern: {level: "local"}
        }));

        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shardSecondary,
            filter: {
                "ns": coll.getFullName(),
                "command.aggregate": coll.getName(),
                "command.comment": "agg_readPref",
                "command.pipeline.$mergeCursors": {"$exists": false},
                "nreturned": {"$exists": true}
            }
        });

        // Find
        assert.commandWorked(mongersDB.runCommand({
            query: {find: "view", comment: "find_readPref", maxTimeMS: 5 * 60 * 1000},
            $readPreference: {mode: "nearest", tags: [{tag: "secondary"}]},
            readConcern: {level: "local"}
        }));

        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shardSecondary,
            filter: {
                "ns": coll.getFullName(),
                "command.aggregate": coll.getName(),
                "command.comment": "find_readPref",
                "command.pipeline.$mergeCursors": {"$exists": false},
                "nreturned": {"$exists": true}
            }
        });

        // Count
        assert.commandWorked(mongersDB.runCommand({
            query: {count: "view", comment: "count_readPref"},
            $readPreference: {mode: "nearest", tags: [{tag: "secondary"}]},
            readConcern: {level: "local"}
        }));

        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shardSecondary,
            filter: {
                "ns": coll.getFullName(),
                "command.aggregate": coll.getName(),
                "command.comment": "count_readPref",
                "command.pipeline.$mergeCursors": {"$exists": false},
                "nreturned": {"$exists": true}
            }
        });

        // Distinct
        assert.commandWorked(mongersDB.runCommand({
            query: {distinct: "view", key: "a", comment: "distinct_readPref"},
            $readPreference: {mode: "nearest", tags: [{tag: "secondary"}]},
            readConcern: {level: "local"}
        }));

        profilerHasSingleMatchingEntryOrThrow({
            profileDB: shardSecondary,
            filter: {
                "ns": coll.getFullName(),
                "command.aggregate": coll.getName(),
                "command.comment": "distinct_readPref",
                "command.pipeline.$mergeCursors": {"$exists": false},
                "nreturned": {"$exists": true}
            }
        });

        assert.commandWorked(shardSecondary.setProfilingLevel(0));
    }

    confirmOptionsInProfiler(st.rs1.getPrimary().getDB(mongersDB.getName()));

    confirmReadPreference(st.rs0.getSecondary().getDB(mongersDB.getName()));
    confirmReadPreference(st.rs1.getSecondary().getDB(mongersDB.getName()));

    st.stop();
})();
