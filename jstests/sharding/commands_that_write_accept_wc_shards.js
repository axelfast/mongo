/**
 * This file tests that commands that do writes accept a write concern in a sharded cluster. This
 * test defines various database commands and what they expect to be true before and after the fact.
 * It then runs the commands with an invalid writeConcern and a valid writeConcern and
 * ensures that they succeed and fail appropriately. This only tests functions that aren't run
 * on config servers.
 *
 * This test is labeled resource intensive because its total io_write is 58MB compared to a median
 * of 5MB across all sharding tests in wiredTiger.
 * @tags: [resource_intensive]
 */
load('jstests/libs/write_concern_util.js');

(function() {
    "use strict";
    var st = new ShardingTest({
        // Set priority of secondaries to zero to prevent spurious elections.
        shards: {
            rs0: {
                nodes: [{}, {rsConfig: {priority: 0}}, {rsConfig: {priority: 0}}],
                settings: {chainingAllowed: false}
            },
            rs1: {
                nodes: [{}, {rsConfig: {priority: 0}}, {rsConfig: {priority: 0}}],
                settings: {chainingAllowed: false}
            }
        },
        configReplSetTestOptions: {settings: {chainingAllowed: false}},
        mongers: 1,
    });

    var mongers = st.s;
    var dbName = "wc-test-shards";
    var db = mongers.getDB(dbName);
    var collName = 'leaves';
    var coll = db[collName];

    function dropTestDatabase() {
        db.runCommand({dropDatabase: 1});
        db.extra.insert({a: 1});
        coll = db[collName];
        st.ensurePrimaryShard(db.toString(), st.shard0.shardName);
        assert.eq(0, coll.find().itcount(), "test collection not empty");
        assert.eq(1, db.extra.find().itcount(), "extra collection should have 1 document");
    }

    var commands = [];

    // Tests a runOnAllShardsCommand against a sharded collection.
    commands.push({
        req: {createIndexes: collName, indexes: [{key: {'type': 1}, name: 'sharded_type_index'}]},
        setupFunc: function() {
            shardCollectionWithChunks(st, coll);
            coll.insert({type: 'oak', x: -3});
            coll.insert({type: 'maple', x: 23});
            assert.eq(coll.getIndexes().length, 2);
        },
        confirmFunc: function() {
            assert.eq(coll.getIndexes().length, 3);
        },
        admin: false
    });

    // Tests a runOnAllShardsCommand.
    commands.push({
        req: {createIndexes: collName, indexes: [{key: {'type': 1}, name: 'type_index'}]},
        setupFunc: function() {
            coll.insert({type: 'oak'});
            st.ensurePrimaryShard(db.toString(), st.shard0.shardName);
            assert.eq(coll.getIndexes().length, 1);
        },
        confirmFunc: function() {
            assert.eq(coll.getIndexes().length, 2);
        },
        admin: false
    });

    // Tests a batched write command.
    commands.push({
        req: {insert: collName, documents: [{x: -3, type: 'maple'}, {x: 23, type: 'maple'}]},
        setupFunc: function() {
            shardCollectionWithChunks(st, coll);
        },
        confirmFunc: function() {
            assert.eq(coll.count({type: 'maple'}), 2);
        },
        admin: false
    });

    // Tests a passthrough.
    commands.push({
        req: {renameCollection: "renameCollWC.leaves", to: 'renameCollWC.pine_needles'},
        setupFunc: function() {
            db = db.getSiblingDB("renameCollWC");
            // Ensure that database is created.
            db.leaves.insert({type: 'oak'});
            st.ensurePrimaryShard(db.toString(), st.shard0.shardName);
            db.leaves.drop();
            db.pine_needles.drop();
            db.leaves.insert({type: 'oak'});
            assert.eq(db.leaves.count(), 1);
            assert.eq(db.pine_needles.count(), 0);
        },
        confirmFunc: function() {
            assert.eq(db.leaves.count(), 0);
            assert.eq(db.pine_needles.count(), 1);
        },
        admin: true
    });

    commands.push({
        req: {
            update: collName,
            updates: [{
                q: {type: 'oak'},
                u: [{$set: {type: 'ginkgo'}}],
            }],
            writeConcern: {w: 'majority'}
        },
        setupFunc: function() {
            coll.insert({type: 'oak'});
            assert.eq(coll.count({type: 'ginkgo'}), 0);
            assert.eq(coll.count({type: 'oak'}), 1);
        },
        confirmFunc: function() {
            assert.eq(coll.count({type: 'ginkgo'}), 1);
            assert.eq(coll.count({type: 'oak'}), 0);
        },
        admin: false
    });

    commands.push({
        req: {
            findAndModify: collName,
            query: {type: 'oak'},
            update: {$set: {type: 'ginkgo'}},
            writeConcern: {w: 'majority'}
        },
        setupFunc: function() {
            coll.insert({type: 'oak'});
            assert.eq(coll.count({type: 'ginkgo'}), 0);
            assert.eq(coll.count({type: 'oak'}), 1);
        },
        confirmFunc: function() {
            assert.eq(coll.count({type: 'ginkgo'}), 1);
            assert.eq(coll.count({type: 'oak'}), 0);
        },
        admin: false
    });

    commands.push({
        req: {
            findAndModify: collName,
            query: {type: 'oak'},
            update: [{$set: {type: 'ginkgo'}}],
            writeConcern: {w: 'majority'}
        },
        setupFunc: function() {
            coll.insert({type: 'oak'});
            assert.eq(coll.count({type: 'ginkgo'}), 0);
            assert.eq(coll.count({type: 'oak'}), 1);
        },
        confirmFunc: function() {
            assert.eq(coll.count({type: 'ginkgo'}), 1);
            assert.eq(coll.count({type: 'oak'}), 0);
        },
        admin: false
    });

    // MapReduce on an unsharded collection.
    commands.push({
        req: {
            mapReduce: collName,
            map: function() {
                this.tags.forEach(function(z) {
                    emit(z, 1);
                });
            },
            reduce: function(key, values) {
                var count = 0;
                values.forEach(function(v) {
                    count = count + v;
                });
                return count;
            },
            out: "foo"
        },
        setupFunc: function() {
            coll.insert({x: -3, tags: ["a", "b"]});
            coll.insert({x: -7, tags: ["b", "c"]});
            coll.insert({x: 23, tags: ["c", "a"]});
            coll.insert({x: 27, tags: ["b", "c"]});
        },
        confirmFunc: function() {
            assert.eq(db.foo.findOne({_id: 'a'}).value, 2);
            assert.eq(db.foo.findOne({_id: 'b'}).value, 3);
            assert.eq(db.foo.findOne({_id: 'c'}).value, 3);
            db.foo.drop();
        },
        admin: false
    });

    // MapReduce on an unsharded collection with an output to a sharded collection.
    commands.push({
        req: {
            mapReduce: collName,
            map: function() {
                this.tags.forEach(function(z) {
                    emit(z, 1);
                });
            },
            reduce: function(key, values) {
                var count = 0;
                values.forEach(function(v) {
                    count = count + v;
                });
                return count;
            },
            out: {replace: "foo", sharded: true}
        },
        setupFunc: function() {
            db.adminCommand({enablesharding: db.toString()});
            coll.insert({x: -3, tags: ["a", "b"]});
            coll.insert({x: -7, tags: ["b", "c"]});
            coll.insert({x: 23, tags: ["c", "a"]});
            coll.insert({x: 27, tags: ["b", "c"]});
        },
        confirmFunc: function() {
            assert.eq(db.foo.findOne({_id: 'a'}).value, 2);
            assert.eq(db.foo.findOne({_id: 'b'}).value, 3);
            assert.eq(db.foo.findOne({_id: 'c'}).value, 3);
            db.foo.drop();
        },
        admin: false
    });

    // MapReduce on a sharded collection.
    commands.push({
        req: {
            mapReduce: collName,
            map: function() {
                if (!this.tags) {
                    return;
                }
                this.tags.forEach(function(z) {
                    emit(z, 1);
                });
            },
            reduce: function(key, values) {
                var count = 0;
                values.forEach(function(v) {
                    count = count + v;
                });
                return count;
            },
            out: "foo"
        },
        setupFunc: function() {
            shardCollectionWithChunks(st, coll);
            coll.insert({x: -3, tags: ["a", "b"]});
            coll.insert({x: -7, tags: ["b", "c"]});
            coll.insert({x: 23, tags: ["c", "a"]});
            coll.insert({x: 27, tags: ["b", "c"]});
        },
        confirmFunc: function() {
            assert.eq(db.foo.findOne({_id: 'a'}).value, 2);
            assert.eq(db.foo.findOne({_id: 'b'}).value, 3);
            assert.eq(db.foo.findOne({_id: 'c'}).value, 3);
            db.foo.drop();
        },
        admin: false
    });

    // MapReduce on a sharded collection with an output action to an unsharded collection.
    commands.push({
        req: {
            mapReduce: collName,
            map: function() {
                if (!this.tags) {
                    return;
                }
                this.tags.forEach(function(z) {
                    emit(z, 1);
                });
            },
            reduce: function(key, values) {
                var count = 0;
                values.forEach(function(v) {
                    count = count + v;
                });
                return count;
            },
            out: {replace: "foo", sharded: false}
        },
        setupFunc: function() {
            shardCollectionWithChunks(st, coll);
            coll.insert({x: -3, tags: ["a", "b"]});
            coll.insert({x: -7, tags: ["b", "c"]});
            coll.insert({x: 23, tags: ["c", "a"]});
            coll.insert({x: 27, tags: ["b", "c"]});
        },
        confirmFunc: function() {
            assert.eq(db.foo.findOne({_id: 'a'}).value, 2);
            assert.eq(db.foo.findOne({_id: 'b'}).value, 3);
            assert.eq(db.foo.findOne({_id: 'c'}).value, 3);
            db.foo.drop();
        },
        admin: false
    });

    // MapReduce from a sharded collection with an output to a sharded collection.
    commands.push({
        req: {
            mapReduce: collName,
            map: function() {
                if (!this.tags) {
                    return;
                }
                this.tags.forEach(function(z) {
                    emit(z, 1);
                });
            },
            reduce: function(key, values) {
                var count = 0;
                values.forEach(function(v) {
                    count = count + v;
                });
                return count;
            },
            out: {replace: "foo", sharded: true}
        },
        setupFunc: function() {
            shardCollectionWithChunks(st, coll);
            coll.insert({x: -3, tags: ["a", "b"]});
            coll.insert({x: -7, tags: ["b", "c"]});
            coll.insert({x: 23, tags: ["c", "a"]});
            coll.insert({x: 27, tags: ["b", "c"]});
        },
        confirmFunc: function() {
            assert.eq(db.foo.findOne({_id: 'a'}).value, 2);
            assert.eq(db.foo.findOne({_id: 'b'}).value, 3);
            assert.eq(db.foo.findOne({_id: 'c'}).value, 3);
            db.foo.drop();
        },
        admin: false
    });

    function testValidWriteConcern(cmd) {
        cmd.req.writeConcern = {w: 'majority', wtimeout: 5 * 60 * 1000};
        jsTest.log("Testing " + tojson(cmd.req));

        dropTestDatabase();
        cmd.setupFunc();
        var res = runCommandCheckAdmin(db, cmd);
        assert.commandWorked(res);
        assert(!res.writeConcernError,
               'command on a full cluster had writeConcernError: ' + tojson(res));
        cmd.confirmFunc();
    }

    function testInvalidWriteConcern(cmd) {
        cmd.req.writeConcern = {w: 'invalid'};
        jsTest.log("Testing " + tojson(cmd.req));

        dropTestDatabase();
        cmd.setupFunc();
        var res = runCommandCheckAdmin(db, cmd);
        assert.commandWorkedIgnoringWriteConcernErrors(res);
        assertWriteConcernError(res, ErrorCodes.UnknownReplWriteConcern);
        cmd.confirmFunc();
    }

    commands.forEach(function(cmd) {
        testValidWriteConcern(cmd);
        testInvalidWriteConcern(cmd);
    });

    st.stop();
})();
