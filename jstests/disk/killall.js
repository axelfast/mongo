/**
 * Verify that killing an instance of mongerd while it is in a long running computation or infinite
 * loop still leads to clean shutdown, and that said shutdown is prompt.
 *
 * For our purposes, "prompt" is defined as "before stopMongerd() decides to send a SIGKILL", which
 * would not result in a zero return code.
 */

var baseName = "jstests_disk_killall";
var dbpath = MongerRunner.dataPath + baseName;

var mongerd = MongerRunner.runMongerd({dbpath: dbpath});
var db = mongerd.getDB("test");
var collection = db.getCollection(baseName);
assert.writeOK(collection.insert({}));

var awaitShell = startParallelShell(
    "db." + baseName + ".count( { $where: function() { while( 1 ) { ; } } } )", mongerd.port);
sleep(1000);

/**
 * 0 == mongerd's exit code on Windows, or when it receives TERM, HUP or INT signals.  On UNIX
 * variants, stopMongerd sends a TERM signal to mongerd, then waits for mongerd to stop.  If mongerd
 * doesn't stop in a reasonable amount of time, stopMongerd sends a KILL signal, in which case mongerd
 * will not exit cleanly.  We're checking in this assert that mongerd will stop quickly even while
 * evaling an infinite loop in server side js.
 */
var exitCode = MongerRunner.stopMongerd(mongerd);
assert.eq(0, exitCode, "got unexpected exitCode");

// Waits for shell to complete
exitCode = awaitShell({checkExitSuccess: false});
assert.neq(0, exitCode, "expected shell to exit abnormally due to mongerd being terminated");

mongerd = MongerRunner.runMongerd(
    {port: mongerd.port, restart: true, cleanData: false, dbpath: mongerd.dbpath});
db = mongerd.getDB("test");
collection = db.getCollection(baseName);

assert(collection.stats().ok);
assert(collection.drop());

MongerRunner.stopMongerd(mongerd);
