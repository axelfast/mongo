jsTest.log("Testing spaces in mongerdump command-line options...");

var mongerd = MongoRunner.runMongod();
var coll = mongerd.getDB("spaces").coll;
coll.drop();
coll.insert({a: 1});
coll.insert({a: 2});

var query = "{\"a\": {\"$gt\": 1} }";
assert(!MongoRunner.runMongoTool(
    "mongerdump",
    {"host": "127.0.0.1:" + mongerd.port, "db": "spaces", "collection": "coll", "query": query}));

MongoRunner.stopMongod(mongerd);

jsTest.log("Test completed successfully");
