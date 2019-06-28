//
// Tests whether we forget M/R's temporary namespaces for sharded output
//

var st = new ShardingTest({shards: 1, mongers: 1});

var mongers = st.s0;
var admin = mongers.getDB("admin");
var coll = mongers.getCollection("foo.bar");
var outputColl = mongers.getCollection((coll.getDB() + "") + ".mrOutput");

var bulk = coll.initializeUnorderedBulkOp();
for (var i = 0; i < 10; i++) {
    bulk.insert({_id: i, even: (i % 2 == 0)});
}
assert.writeOK(bulk.execute());

var map = function() {
    emit(this.even, 1);
};
var reduce = function(key, values) {
    return Array.sum(values);
};

out = coll.mapReduce(map, reduce, {out: {reduce: outputColl.getName(), sharded: true}});

printjson(out);
printjson(outputColl.find().toArray());

var mongerdThreadStats = st.shard0.getDB("admin").runCommand({shardConnPoolStats: 1}).threads;
var mongersThreadStats = admin.runCommand({shardConnPoolStats: 1}).threads;

printjson(mongerdThreadStats);
printjson(mongersThreadStats);

var checkForSeenNS = function(threadStats, regex) {
    for (var i = 0; i < threadStats.length; i++) {
        var seenNSes = threadStats[i].seenNS;
        for (var j = 0; j < seenNSes.length; j++) {
            assert(!(regex.test(seenNSes)));
        }
    }
};

checkForSeenNS(mongerdThreadStats, /^foo.tmp/);
checkForSeenNS(mongersThreadStats, /^foo.tmp/);

st.stop();
