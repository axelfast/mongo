// SERVER-2111 Check that an in memory db name will block creation of a db with a similar but
// differently cased name.

var dbLowerCase = db.getSisterDB("dbcase2test_dbnamea");
var dbUpperCase = db.getSisterDB("dbcase2test_dbnameA");

var resultLower = dbLowerCase.c.insert({});
assert.eq(1, resultLower.nInserted);

var resultUpper = dbUpperCase.c.insert({});
assert.commandFailed(resultUpper);

assert.eq(-1, db.getMonger().getDBNames().indexOf("dbcase2test_dbnameA"));
