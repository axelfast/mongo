// Tests that resharding a collection is detected correctly by all operation types
//
// The idea here is that a collection may be resharded / unsharded at any point, and any type of
// operation on a mongers may be active when it happens.  All operations should handle gracefully.
//

var st = new ShardingTest({shards: 2, mongers: 5, verbose: 1});
// Balancer is by default stopped, thus it will not interfere

// Use separate mongers for reading, updating, inserting, removing data
var readMongers = st.s1;
var updateMongers = st.s2;
var insertMongers = st.s3;
var removeMongers = st.s4;

var config = st.s.getDB("config");
var admin = st.s.getDB("admin");
var coll = st.s.getCollection("foo.bar");

assert.commandWorked(
    insertMongers.getDB("admin").runCommand({setParameter: 1, traceExceptions: true}));

var shards = [st.shard0, st.shard1];

//
// Set up a sharded collection
//

jsTest.log("Enabling sharding for the first time...");

assert.commandWorked(admin.runCommand({enableSharding: coll.getDB() + ""}));
st.ensurePrimaryShard(coll.getDB().getName(), st.shard1.shardName);
assert.commandWorked(admin.runCommand({shardCollection: coll + "", key: {_id: 1}}));

assert.writeOK(coll.insert({hello: "world"}));

jsTest.log("Sharding collection across multiple shards...");

let res = admin.runCommand({split: coll + "", middle: {_id: 0}});
assert.commandWorked(res);
printjson(res);

res = admin.runCommand({
    moveChunk: coll + "",
    find: {_id: 0},
    to: st.getOther(st.getPrimaryShard(coll.getDB() + "")).name
});
assert.commandWorked(res);
printjson(res);

st.printShardingStatus();

//
// Force all mongerses to load the current status of the cluster
//

jsTest.log("Loading this status in all mongerses...");

for (var i = 0; i < st._mongers.length; i++) {
    res = st._mongers[i].getDB("admin").runCommand({flushRouterConfig: 1});
    assert.commandWorked(res);
    printjson(res);
    assert.neq(null, st._mongers[i].getCollection(coll + "").findOne());
}

//
// Drop and recreate a new sharded collection in the same namespace, where the shard and collection
// versions are the same, but the split is at a different point.
//

jsTest.log("Rebuilding sharded collection with different split...");

coll.drop();

var droppedCollDoc = config.collections.findOne({_id: coll.getFullName()});
assert(droppedCollDoc != null);
assert.eq(true, droppedCollDoc.dropped);
assert(droppedCollDoc.lastmodEpoch != null);
assert(droppedCollDoc.lastmodEpoch.equals(new ObjectId("000000000000000000000000")),
       "epoch not zero: " + droppedCollDoc.lastmodEpoch);

assert.commandWorked(admin.runCommand({enableSharding: coll.getDB() + ""}));
st.ensurePrimaryShard(coll.getDB().getName(), st.shard1.shardName);
assert.commandWorked(admin.runCommand({shardCollection: coll + "", key: {_id: 1}}));

var bulk = coll.initializeUnorderedBulkOp();
for (var i = 0; i < 100; i++)
    bulk.insert({_id: i});
assert.writeOK(bulk.execute());

res = admin.runCommand({split: coll + "", middle: {_id: 200}});
assert.commandWorked(res);
printjson(res);

res = admin.runCommand({
    moveChunk: coll + "",
    find: {_id: 200},
    to: st.getOther(st.getPrimaryShard(coll.getDB() + "")).name
});
assert.commandWorked(res);
printjson(res);

//
// Make sure all operations on mongerses aren't tricked by the change
//

jsTest.log("Checking other mongerses for detection of change...");

jsTest.log("Checking find...");
// Ensure that finding an element works when resharding
assert.neq(null, readMongers.getCollection(coll + "").findOne({_id: 1}));

jsTest.log("Checking update...");
// Ensure that updating an element finds the right location
assert.writeOK(updateMongers.getCollection(coll + "").update({_id: 1}, {$set: {updated: true}}));
assert.neq(null, coll.findOne({updated: true}));

jsTest.log("Checking insert...");
// Ensure that inserting an element finds the right shard
assert.writeOK(insertMongers.getCollection(coll + "").insert({_id: 101}));
assert.neq(null, coll.findOne({_id: 101}));

jsTest.log("Checking remove...");
// Ensure that removing an element finds the right shard, verified by the mongers doing the sharding
assert.writeOK(removeMongers.getCollection(coll + "").remove({_id: 2}));
assert.eq(null, coll.findOne({_id: 2}));

coll.drop();

jsTest.log("Done!");

st.stop();
