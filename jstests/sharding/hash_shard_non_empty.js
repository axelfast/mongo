// Hash sharding on a non empty collection should not pre-split.

var s = new ShardingTest({name: jsTestName(), shards: 3, mongers: 1, verbose: 1});
var dbname = "test";
var coll = "foo";
var db = s.getDB(dbname);
db.adminCommand({enablesharding: dbname});
s.ensurePrimaryShard('test', s.shard1.shardName);

// for simplicity turn off balancer
s.stopBalancer();

db.getCollection(coll).insert({a: 1});

db.getCollection(coll).ensureIndex({a: "hashed"});
var res = db.adminCommand({shardcollection: dbname + "." + coll, key: {a: "hashed"}});
assert.eq(res.ok, 1, "shardcollection didn't work");
s.printShardingStatus();
var numChunks = s.config.chunks.count({"ns": "test.foo"});
assert.eq(numChunks, 1, "sharding non-empty collection should not pre-split");

s.stop();
