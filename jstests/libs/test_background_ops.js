//
// Utilities related to background operations while other operations are working
//

/**
 * Allows synchronization between background ops and the test operations
 */
var waitForLock = function(monger, name) {

    var ts = new ObjectId();
    var lockColl = monger.getCollection("config.testLocks");

    lockColl.update({_id: name, state: 0}, {$set: {state: 0}}, true);

    //
    // Wait until we can set the state to 1 with our id
    //

    var startTime = new Date().getTime();

    assert.soon(function() {
        lockColl.update({_id: name, state: 0}, {$set: {ts: ts, state: 1}});
        var gleObj = lockColl.getDB().getLastErrorObj();

        if (new Date().getTime() - startTime > 20 * 1000) {
            print("Waiting for...");
            printjson(gleObj);
            printjson(lockColl.findOne());
            printjson(ts);
        }

        return gleObj.n == 1 || gleObj.updatedExisting;
    }, "could not acquire lock", 30 * 1000, 100);

    print("Acquired lock " + tojson({_id: name, ts: ts}) + " curr : " +
          tojson(lockColl.findOne({_id: name})));

    // Set the state back to 0
    var unlock = function() {
        print("Releasing lock " + tojson({_id: name, ts: ts}) + " curr : " +
              tojson(lockColl.findOne({_id: name})));
        lockColl.update({_id: name, ts: ts}, {$set: {state: 0}});
    };

    // Return an object we can invoke unlock on
    return {unlock: unlock};
};

/**
 * Allows a test or background op to say it's finished
 */
var setFinished = function(monger, name, finished) {
    if (finished || finished == undefined)
        monger.getCollection("config.testFinished").update({_id: name}, {_id: name}, true);
    else
        monger.getCollection("config.testFinished").remove({_id: name});
};

/**
 * Checks whether a test or background op is finished
 */
var isFinished = function(monger, name) {
    return monger.getCollection("config.testFinished").findOne({_id: name}) != null;
};

/**
 * Sets the result of a background op
 */
var setResult = function(monger, name, result, err) {
    monger.getCollection("config.testResult")
        .update({_id: name}, {_id: name, result: result, err: err}, true);
};

/**
 * Gets the result for a background op
 */
var getResult = function(monger, name) {
    return monger.getCollection("config.testResult").findOne({_id: name});
};

/**
 * Overrides the parallel shell code in monger
 */
function startParallelShell(jsCode, port) {
    if (TestData) {
        jsCode = "TestData = " + tojson(TestData) + ";" + jsCode;
    }

    var x;
    if (port) {
        x = startMongerProgramNoConnect("monger", "--port", port, "--eval", jsCode);
    } else {
        x = startMongerProgramNoConnect("monger", "--eval", jsCode, db ? db.getMonger().host : null);
    }

    return function() {
        jsTestLog("Waiting for shell " + x + "...");
        waitProgram(x);
        jsTestLog("Shell " + x + " finished.");
    };
}

startParallelOps = function(monger, proc, args, context) {

    var procName = proc.name + "-" + new ObjectId();
    var seed = new ObjectId(new ObjectId().valueOf().split("").reverse().join(""))
                   .getTimestamp()
                   .getTime();

    // Make sure we aren't finished before we start
    setFinished(monger, procName, false);
    setResult(monger, procName, undefined, undefined);

    // TODO: Make this a context of its own
    var procContext = {
        procName: procName,
        seed: seed,
        waitForLock: waitForLock,
        setFinished: setFinished,
        isFinished: isFinished,
        setResult: setResult,

        setup: function(context, stored) {

            waitForLock = function() {
                return context.waitForLock(db.getMonger(), context.procName);
            };
            setFinished = function(finished) {
                return context.setFinished(db.getMonger(), context.procName, finished);
            };
            isFinished = function() {
                return context.isFinished(db.getMonger(), context.procName);
            };
            setResult = function(result, err) {
                return context.setResult(db.getMonger(), context.procName, result, err);
            };
        }
    };

    var bootstrapper = function(stored) {

        var procContext = stored.procContext;
        eval("procContext = " + procContext);
        procContext.setup(procContext, stored);

        var contexts = stored.contexts;
        eval("contexts = " + contexts);

        for (var i = 0; i < contexts.length; i++) {
            if (typeof(contexts[i]) != "undefined") {
                // Evaluate all contexts
                contexts[i](procContext);
            }
        }

        var operation = stored.operation;
        eval("operation = " + operation);

        var args = stored.args;
        eval("args = " + args);

        result = undefined;
        err = undefined;

        try {
            result = operation.apply(null, args);
        } catch (e) {
            err = e;
        }

        setResult(result, err);
    };

    var contexts = [RandomFunctionContext, context];

    var testDataColl = monger.getCollection("config.parallelTest");

    testDataColl.insert({
        _id: procName,
        bootstrapper: tojson(bootstrapper),
        operation: tojson(proc),
        args: tojson(args),
        procContext: tojson(procContext),
        contexts: tojson(contexts)
    });

    assert.eq(null, testDataColl.getDB().getLastError());

    var bootstrapStartup = "{ var procName = '" + procName + "'; " +
        "var stored = db.getMonger().getCollection( '" + testDataColl + "' )" +
        ".findOne({ _id : procName }); " + "var bootstrapper = stored.bootstrapper; " +
        "eval( 'bootstrapper = ' + bootstrapper ); " + "bootstrapper( stored ); " + "}";

    // Save the global db object if it exists, so that we can restore it after starting the parallel
    // shell.
    var oldDB = undefined;
    if (typeof db !== 'undefined') {
        oldDB = db;
    }
    db = monger.getDB("test");

    jsTest.log("Starting " + proc.name + " operations...");

    var rawJoin = startParallelShell(bootstrapStartup);

    db = oldDB;

    var join = function() {
        setFinished(monger, procName, true);

        rawJoin();
        result = getResult(monger, procName);

        assert.neq(result, null);

        if (result.err)
            throw Error("Error in parallel ops " + procName + " : " + tojson(result.err));

        else
            return result.result;
    };

    join.isFinished = function() {
        return isFinished(monger, procName);
    };

    join.setFinished = function(finished) {
        return setFinished(monger, procName, finished);
    };

    join.waitForLock = function(name) {
        return waitForLock(monger, name);
    };

    return join;
};

var RandomFunctionContext = function(context) {

    Random.srand(context.seed);

    Random.randBool = function() {
        return Random.rand() > 0.5;
    };

    Random.randInt = function(min, max) {

        if (max == undefined) {
            max = min;
            min = 0;
        }

        return min + Math.floor(Random.rand() * max);
    };

    Random.randShardKey = function() {

        var numFields = 2;  // Random.randInt(1, 3)

        var key = {};
        for (var i = 0; i < numFields; i++) {
            var field = String.fromCharCode("a".charCodeAt() + i);
            key[field] = 1;
        }

        return key;
    };

    Random.randShardKeyValue = function(shardKey) {

        var keyValue = {};
        for (field in shardKey) {
            keyValue[field] = Random.randInt(1, 100);
        }

        return keyValue;
    };

    Random.randCluster = function() {

        var numShards = 2;  // Random.randInt( 1, 10 )
        var rs = false;     // Random.randBool()
        var st = new ShardingTest({shards: numShards, mongers: 4, other: {rs: rs}});

        return st;
    };
};

//
// Some utility operations
//

function moveOps(collName, options) {
    options = options || {};

    var admin = db.getMonger().getDB("admin");
    var config = db.getMonger().getDB("config");
    var shards = config.shards.find().toArray();
    var shardKey = config.collections.findOne({_id: collName}).key;

    while (!isFinished()) {
        var findKey = Random.randShardKeyValue(shardKey);
        var toShard = shards[Random.randInt(shards.length)]._id;

        try {
            printjson(admin.runCommand({moveChunk: collName, find: findKey, to: toShard}));
        } catch (e) {
            printjson(e);
        }

        sleep(1000);
    }

    jsTest.log("Stopping moveOps...");
}

function splitOps(collName, options) {
    options = options || {};

    var admin = db.getMonger().getDB("admin");
    var config = db.getMonger().getDB("config");
    var shards = config.shards.find().toArray();
    var shardKey = config.collections.findOne({_id: collName}).key;

    while (!isFinished()) {
        var middleKey = Random.randShardKeyValue(shardKey);

        try {
            printjson(admin.runCommand({split: collName, middle: middleKey}));
        } catch (e) {
            printjson(e);
        }

        sleep(1000);
    }

    jsTest.log("Stopping splitOps...");
}
