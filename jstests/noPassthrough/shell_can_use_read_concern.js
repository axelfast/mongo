/**
 * Tests that read operations executed through the monger shell's API are specify afterClusterTime
 * when causal consistency is enabled.
 * @tags: [requires_replication]
 */
(function() {
    "use strict";

    // This test makes assertions on commands run without logical session ids.
    TestData.disableImplicitSessions = true;

    const rst = new ReplSetTest({nodes: 1});
    rst.startSet();
    rst.initiate();

    const primary = rst.getPrimary();

    function runTests({withSession}) {
        let db;

        if (withSession) {
            primary.setCausalConsistency(false);
            db = primary.startSession({causalConsistency: true}).getDatabase("test");
        } else {
            primary.setCausalConsistency(true);
            db = primary.getDB("test");
        }

        const coll = db.shell_can_use_read_concern;
        coll.drop();

        function testCommandCanBeCausallyConsistent(func, {
            expectedSession: expectedSession = withSession,
            expectedAfterClusterTime: expectedAfterClusterTime = true
        } = {}) {
            const mongerRunCommandOriginal = Monger.prototype.runCommand;

            const sentinel = {};
            let cmdObjSeen = sentinel;

            Monger.prototype.runCommand = function runCommandSpy(dbName, cmdObj, options) {
                cmdObjSeen = cmdObj;
                return mongerRunCommandOriginal.apply(this, arguments);
            };

            try {
                assert.doesNotThrow(func);
            } finally {
                Monger.prototype.runCommand = mongerRunCommandOriginal;
            }

            if (cmdObjSeen === sentinel) {
                throw new Error("Monger.prototype.runCommand() was never called: " +
                                func.toString());
            }

            let cmdName = Object.keys(cmdObjSeen)[0];

            // If the command is in a wrapped form, then we look for the actual command object
            // inside
            // the query/$query object.
            if (cmdName === "query" || cmdName === "$query") {
                cmdObjSeen = cmdObjSeen[cmdName];
                cmdName = Object.keys(cmdObjSeen)[0];
            }

            if (expectedSession) {
                assert(cmdObjSeen.hasOwnProperty("lsid"),
                       "Expected operation " + tojson(cmdObjSeen) +
                           " to have a logical session id: " + func.toString());
            } else {
                assert(!cmdObjSeen.hasOwnProperty("lsid"),
                       "Expected operation " + tojson(cmdObjSeen) +
                           " to not have a logical session id: " + func.toString());
            }

            if (expectedAfterClusterTime) {
                assert(cmdObjSeen.hasOwnProperty("readConcern"),
                       "Expected operation " + tojson(cmdObjSeen) +
                           " to have a readConcern object since it can be causally consistent: " +
                           func.toString());

                const readConcern = cmdObjSeen.readConcern;
                assert(readConcern.hasOwnProperty("afterClusterTime"),
                       "Expected operation " + tojson(cmdObjSeen) +
                           " to specify afterClusterTime since it can be causally consistent: " +
                           func.toString());
            } else {
                assert(!cmdObjSeen.hasOwnProperty("readConcern"),
                       "Expected operation " + tojson(cmdObjSeen) + " to not have a readConcern" +
                           " object since it cannot be causally consistent: " + func.toString());
            }
        }

        //
        // Tests for the "find" and "getMore" commands.
        //

        {
            testCommandCanBeCausallyConsistent(function() {
                assert.writeOK(coll.insert([{}, {}, {}, {}, {}]));
            }, {expectedSession: withSession, expectedAfterClusterTime: false});

            testCommandCanBeCausallyConsistent(function() {
                assert.commandWorked(
                    db.runCommand({find: coll.getName(), batchSize: 5, singleBatch: true}));
            });

            const cursor = coll.find().batchSize(2);

            testCommandCanBeCausallyConsistent(function() {
                cursor.next();
                cursor.next();
            });

            testCommandCanBeCausallyConsistent(function() {
                cursor.next();
                cursor.next();
                cursor.next();
                assert(!cursor.hasNext());
            }, {
                expectedSession: withSession,
                expectedAfterClusterTime: false,
            });
        }

        //
        // Tests for the "count" command.
        //

        testCommandCanBeCausallyConsistent(function() {
            assert.commandWorked(db.runCommand({count: coll.getName()}));
        });

        testCommandCanBeCausallyConsistent(function() {
            assert.commandWorked(db.runCommand({query: {count: coll.getName()}}));
        });

        testCommandCanBeCausallyConsistent(function() {
            assert.commandWorked(db.runCommand({$query: {count: coll.getName()}}));
        });

        testCommandCanBeCausallyConsistent(function() {
            assert.eq(5, coll.count());
        });

        //
        // Tests for the "distinct" command.
        //

        testCommandCanBeCausallyConsistent(function() {
            assert.commandWorked(db.runCommand({distinct: coll.getName(), key: "_id"}));
        });

        testCommandCanBeCausallyConsistent(function() {
            const values = coll.distinct("_id");
            assert.eq(5, values.length, tojson(values));
        });

        //
        // Tests for the "aggregate" command.
        //

        {
            testCommandCanBeCausallyConsistent(function() {
                assert.commandWorked(db.runCommand(
                    {aggregate: coll.getName(), pipeline: [], cursor: {batchSize: 5}}));
            });

            testCommandCanBeCausallyConsistent(function() {
                assert.commandWorked(db.runCommand({
                    aggregate: coll.getName(),
                    pipeline: [],
                    cursor: {batchSize: 5},
                    explain: true
                }));
            });

            let cursor;

            testCommandCanBeCausallyConsistent(function() {
                cursor = coll.aggregate([], {cursor: {batchSize: 2}});
                cursor.next();
                cursor.next();
            });

            testCommandCanBeCausallyConsistent(function() {
                cursor.next();
                cursor.next();
                cursor.next();
                assert(!cursor.hasNext());
            }, {
                expectedSession: withSession,
                expectedAfterClusterTime: false,
            });
        }

        //
        // Tests for the "geoSearch" command.
        //

        testCommandCanBeCausallyConsistent(function() {
            assert.commandWorked(coll.createIndex({loc: "geoHaystack", other: 1}, {bucketSize: 1}));
        }, {expectedSession: withSession, expectedAfterClusterTime: false});

        testCommandCanBeCausallyConsistent(function() {
            assert.commandWorked(db.runCommand(
                {geoSearch: coll.getName(), near: [0, 0], maxDistance: 1, search: {}}));
        });

        //
        // Tests for the "explain" command.
        //

        testCommandCanBeCausallyConsistent(function() {
            assert.commandWorked(db.runCommand({explain: {find: coll.getName()}}));
        });

        testCommandCanBeCausallyConsistent(function() {
            coll.find().explain();
        });

        testCommandCanBeCausallyConsistent(function() {
            coll.explain().find().finish();
        });

        db.getSession().endSession();
    }

    runTests({withSession: false});
    runTests({withSession: true});

    rst.stopSet();
})();
