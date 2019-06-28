//
// Test checks whether the balancer correctly detects a mixed set of shards
//

jsTest.log("Starting cluster...");

var options = {
    mongersOptions: {verbose: 1, useLogFiles: true},
    configOptions: {},
    shardOptions: {binVersion: ["latest", "last-stable"]},
    enableBalancer: true,
    other: {shardAsReplicaSet: false}
};

var st = new ShardingTest({shards: 3, mongers: 1, other: options});

var mongers = st.s0;
var admin = mongers.getDB("admin");
var coll = mongers.getCollection("foo.bar");

printjson(admin.runCommand({enableSharding: coll.getDB() + ""}));
st.ensurePrimaryShard(coll.getDB().getName(), st.shard1.shardName);
printjson(admin.runCommand({shardCollection: coll + "", key: {_id: 1}}));

assert.soon(function() {
    var log = cat(mongers.fullOptions.logFile);
    return /multiVersion cluster detected/.test(log);
}, "multiVersion warning not printed!", 30 * 16 * 60 * 1000, 5 * 1000);

jsTest.log("DONE!");

st.stop();
