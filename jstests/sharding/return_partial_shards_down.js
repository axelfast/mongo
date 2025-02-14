//
// Tests that zero results are correctly returned with returnPartial and shards down
//

// Checking UUID consistency involves talking to shards, but this test shuts down shards.
TestData.skipCheckingUUIDsConsistentAcrossCluster = true;

// TODO: SERVER-33597 remove shardAsReplicaSet: false
var st = new ShardingTest(
    {shards: 3, mongers: 1, other: {mongersOptions: {verbose: 2}, shardAsReplicaSet: false}});

// Stop balancer, we're doing our own manual chunk distribution
st.stopBalancer();

var mongers = st.s;
var admin = mongers.getDB("admin");

var collOneShard = mongers.getCollection("foo.collOneShard");
var collAllShards = mongers.getCollection("foo.collAllShards");

printjson(admin.runCommand({enableSharding: collOneShard.getDB() + ""}));
printjson(admin.runCommand({movePrimary: collOneShard.getDB() + "", to: st.shard0.shardName}));

printjson(admin.runCommand({shardCollection: collOneShard + "", key: {_id: 1}}));
printjson(admin.runCommand({shardCollection: collAllShards + "", key: {_id: 1}}));

// Split and move the "both shard" collection to both shards

printjson(admin.runCommand({split: collAllShards + "", middle: {_id: 0}}));
printjson(admin.runCommand({split: collAllShards + "", middle: {_id: 1000}}));
printjson(
    admin.runCommand({moveChunk: collAllShards + "", find: {_id: 0}, to: st.shard1.shardName}));
printjson(
    admin.runCommand({moveChunk: collAllShards + "", find: {_id: 1000}, to: st.shard2.shardName}));

// Collections are now distributed correctly
jsTest.log("Collections now distributed correctly.");
st.printShardingStatus();

var inserts = [{_id: -1}, {_id: 1}, {_id: 1000}];

collOneShard.insert(inserts);
assert.writeOK(collAllShards.insert(inserts));

var returnPartialFlag = 1 << 7;

jsTest.log("All shards up!");

assert.eq(3, collOneShard.find().itcount());
assert.eq(3, collAllShards.find().itcount());

assert.eq(3, collOneShard.find({}, {}, 0, 0, 0, returnPartialFlag).itcount());
assert.eq(3, collAllShards.find({}, {}, 0, 0, 0, returnPartialFlag).itcount());

jsTest.log("One shard down!");

MongerRunner.stopMongerd(st.shard2);

jsTest.log("done.");

assert.eq(3, collOneShard.find({}, {}, 0, 0, 0, returnPartialFlag).itcount());
assert.eq(2, collAllShards.find({}, {}, 0, 0, 0, returnPartialFlag).itcount());

jsTest.log("Two shards down!");

MongerRunner.stopMongerd(st.shard1);

jsTest.log("done.");

assert.eq(3, collOneShard.find({}, {}, 0, 0, 0, returnPartialFlag).itcount());
assert.eq(1, collAllShards.find({}, {}, 0, 0, 0, returnPartialFlag).itcount());

jsTest.log("All shards down!");

MongerRunner.stopMongerd(st.shard0);

jsTest.log("done.");

assert.eq(0, collOneShard.find({}, {}, 0, 0, 0, returnPartialFlag).itcount());
assert.eq(0, collAllShards.find({}, {}, 0, 0, 0, returnPartialFlag).itcount());

jsTest.log("DONE!");

st.stop();
