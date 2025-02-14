// Test that map reduce and aggregate properly handle shard versioning.
(function() {
    "use strict";

    var st = new ShardingTest({shards: 2, mongers: 3});

    var dbName = jsTest.name();
    var collName = dbName + ".coll";
    var numDocs = 50000;
    var numKeys = 1000;

    st.s.adminCommand({enableSharding: dbName});
    st.ensurePrimaryShard(dbName, st.shard0.shardName);
    st.s.adminCommand({shardCollection: collName, key: {key: 1}});

    // Load chunk data to the stale mongerses before moving a chunk
    var staleMongers1 = st.s1;
    var staleMongers2 = st.s2;
    staleMongers1.getCollection(collName).find().itcount();
    staleMongers2.getCollection(collName).find().itcount();

    st.s.adminCommand({split: collName, middle: {key: numKeys / 2}});
    st.s.adminCommand({moveChunk: collName, find: {key: 0}, to: st.shard1.shardName});

    var bulk = st.s.getCollection(collName).initializeUnorderedBulkOp();
    for (var i = 0; i < numDocs; i++) {
        bulk.insert({_id: i, key: (i % numKeys), value: i % numKeys});
    }
    assert.writeOK(bulk.execute());

    // Add orphaned documents directly to the shards to ensure they are properly filtered out.
    st.shard0.getCollection(collName).insert({_id: 0, key: 0, value: 0});
    st.shard1.getCollection(collName).insert({_id: numDocs, key: numKeys, value: numKeys});

    jsTest.log("Doing mapReduce");

    var map = function() {
        emit(this.key, this.value);
    };
    var reduce = function(k, values) {
        var total = 0;
        for (var i = 0; i < values.length; i++) {
            total += values[i];
        }
        return total;
    };
    function validateOutput(output) {
        assert.eq(output.length, numKeys, tojson(output));
        for (var i = 0; i < output.length; i++) {
            assert.eq(output[i]._id * (numDocs / numKeys), output[i].value, tojson(output));
        }
    }

    var res = staleMongers1.getCollection(collName).mapReduce(map, reduce, {out: {inline: 1}});
    validateOutput(res.results);

    jsTest.log("Doing aggregation");

    res = staleMongers2.getCollection(collName).aggregate(
        [{'$group': {_id: "$key", value: {"$sum": "$value"}}}, {'$sort': {_id: 1}}]);
    validateOutput(res.toArray());

    st.stop();

})();
