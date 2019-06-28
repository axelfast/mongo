// This test checks that w:"majority" works correctly on a lone mongerd

// set up a mongerd and connect
var mongerd = MongoRunner.runMongod({});

// get db and collection, then perform a trivial insert
db = mongerd.getDB("test");
col = db.getCollection("single_server_majority");
col.drop();

// see if we can get a majority write on this single server
assert.writeOK(col.save({a: "test"}, {writeConcern: {w: 'majority'}}));
MongoRunner.stopMongod(mongerd);