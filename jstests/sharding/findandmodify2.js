(function() {
    'use strict';
    load('jstests/sharding/autosplit_include.js');

    var s = new ShardingTest({shards: 2, mongers: 1, other: {chunkSize: 1, enableAutoSplit: true}});
    assert.commandWorked(s.s0.adminCommand({enablesharding: "test"}));

    var db = s.getDB("test");
    s.ensurePrimaryShard('test', s.shard1.shardName);
    var primary = s.getPrimaryShard("test").getDB("test");
    var secondary = s.getOther(primary).getDB("test");

    var n = 100;
    var collection = "stuff";
    var minChunks = 2;

    var col_update = collection + '_col_update';
    var col_update_upsert = col_update + '_upsert';
    var col_fam = collection + '_col_fam';
    var col_fam_upsert = col_fam + '_upsert';

    var big = "x";
    for (var i = 0; i < 15; i++) {
        big += big;
    }

    // drop the collection
    db[col_update].drop();
    db[col_update_upsert].drop();
    db[col_fam].drop();
    db[col_fam_upsert].drop();

    // shard the collection on _id
    s.adminCommand({shardcollection: 'test.' + col_update, key: {_id: 1}});
    s.adminCommand({shardcollection: 'test.' + col_update_upsert, key: {_id: 1}});
    s.adminCommand({shardcollection: 'test.' + col_fam, key: {_id: 1}});
    s.adminCommand({shardcollection: 'test.' + col_fam_upsert, key: {_id: 1}});

    // update via findAndModify
    function via_fam() {
        for (var i = 0; i < n; i++) {
            db[col_fam].save({_id: i});
        }

        for (var i = 0; i < n; i++) {
            db[col_fam].findAndModify({query: {_id: i}, update: {$set: {big: big}}});
        }
    }

    // upsert via findAndModify
    function via_fam_upsert() {
        for (var i = 0; i < n; i++) {
            db[col_fam_upsert].findAndModify(
                {query: {_id: i}, update: {$set: {big: big}}, upsert: true});
        }
    }

    // update data using basic update
    function via_update() {
        for (var i = 0; i < n; i++) {
            db[col_update].save({_id: i});
        }

        for (var i = 0; i < n; i++) {
            db[col_update].update({_id: i}, {$set: {big: big}});
        }
    }

    // upsert data using basic update
    function via_update_upsert() {
        for (var i = 0; i < n; i++) {
            db[col_update_upsert].update({_id: i}, {$set: {big: big}}, true);
        }
    }

    print("---------- Update via findAndModify...");
    via_fam();
    waitForOngoingChunkSplits(s);

    print("---------- Done.");

    print("---------- Upsert via findAndModify...");
    via_fam_upsert();
    waitForOngoingChunkSplits(s);

    print("---------- Done.");

    print("---------- Basic update...");
    via_update();
    waitForOngoingChunkSplits(s);

    print("---------- Done.");

    print("---------- Basic update with upsert...");
    via_update_upsert();
    waitForOngoingChunkSplits(s);

    print("---------- Done.");

    print("---------- Printing chunks:");
    s.printChunks();

    print("---------- Verifying that both codepaths resulted in splits...");
    assert.gte(s.config.chunks.count({"ns": "test." + col_fam}),
               minChunks,
               "findAndModify update code path didn't result in splits");
    assert.gte(s.config.chunks.count({"ns": "test." + col_fam_upsert}),
               minChunks,
               "findAndModify upsert code path didn't result in splits");
    assert.gte(s.config.chunks.count({"ns": "test." + col_update}),
               minChunks,
               "update code path didn't result in splits");
    assert.gte(s.config.chunks.count({"ns": "test." + col_update_upsert}),
               minChunks,
               "upsert code path didn't result in splits");

    printjson(db[col_update].stats());

    // ensure that all chunks are smaller than chunkSize
    // make sure not teensy
    // test update without upsert and with upsert

    s.stop();
})();
