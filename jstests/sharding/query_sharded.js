//
// Tests mongers-only query behavior
//

var st = new ShardingTest({shards: 1, mongers: 1, verbose: 0});

var mongers = st.s0;
var coll = mongers.getCollection("foo.bar");

//
//
// Ensure we can't use exhaust option through mongers
coll.remove({});
assert.writeOK(coll.insert({a: 'b'}));
var query = coll.find({});
assert.neq(null, query.next());
query = coll.find({}).addOption(DBQuery.Option.exhaust);
assert.throws(function() {
    query.next();
});

//
//
// Ensure we can't trick mongers by inserting exhaust option on a command through mongers
coll.remove({});
assert.writeOK(coll.insert({a: 'b'}));
var cmdColl = mongers.getCollection(coll.getDB().toString() + ".$cmd");
var cmdQuery = cmdColl.find({ping: 1}).limit(1);
assert.commandWorked(cmdQuery.next());
cmdQuery = cmdColl.find({ping: 1}).limit(1).addOption(DBQuery.Option.exhaust);
assert.throws(function() {
    assert.commandWorked(cmdQuery.next());
});

jsTest.log("DONE!");

st.stop();
