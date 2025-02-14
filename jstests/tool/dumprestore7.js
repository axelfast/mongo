(function() {
    "use strict";

    // Skip this test if running with --nojournal and WiredTiger.
    if (jsTest.options().noJournal &&
        (!jsTest.options().storageEngine || jsTest.options().storageEngine === "wiredTiger")) {
        print("Skipping test because running WiredTiger without journaling isn't a valid" +
              " replica set configuration");
        return;
    }

    var name = "dumprestore7";

    var step = (function() {
        var n = 0;
        return function(msg) {
            msg = msg || "";
            print('\n' + name + ".js step " + (++n) + ' ' + msg);
        };
    })();

    step("starting the replset test");

    var replTest = new ReplSetTest({name: name, nodes: 1});
    var nodes = replTest.startSet();
    replTest.initiate();

    step("inserting first chunk of data");
    var foo = replTest.getPrimary().getDB("foo");
    for (var i = 0; i < 20; i++) {
        foo.bar.insert({x: i, y: "abc"});
    }

    step("waiting for replication");
    replTest.awaitReplication();
    assert.eq(foo.bar.count(), 20, "should have inserted 20 documents");

    // The time of the last oplog entry.
    var time = replTest.getPrimary()
                   .getDB("local")
                   .getCollection("oplog.rs")
                   .find()
                   .limit(1)
                   .sort({$natural: -1})
                   .next()
                   .ts;
    step("got time of last oplog entry: " + time);

    step("inserting second chunk of data");
    for (var i = 30; i < 50; i++) {
        foo.bar.insert({x: i, y: "abc"});
    }
    assert.eq(foo.bar.count(), 40, "should have inserted 40 total documents");

    step("try mongerdump with $timestamp");

    var data = MongerRunner.dataDir + "/dumprestore7-dump1/";
    var query = {ts: {$gt: time}};
    print("mongerdump query: " + tojson(query));

    var testQueryCount =
        replTest.getPrimary().getDB("local").getCollection("oplog.rs").find(query).itcount();
    assert.eq(testQueryCount, 20, "the query should match 20 documents");

    var exitCode = MongerRunner.runMongerTool("mongerdump", {
        host: "127.0.0.1:" + replTest.ports[0],
        db: "local",
        collection: "oplog.rs",
        query: tojson(query),
        out: data,
    });
    assert.eq(0, exitCode, "monogdump failed to dump the oplog");

    step("try mongerrestore from $timestamp");

    var restoreMongerd = MongerRunner.runMongerd({});
    exitCode = MongerRunner.runMongerTool("mongerrestore", {
        host: "127.0.0.1:" + restoreMongerd.port,
        dir: data,
        writeConcern: 1,
    });
    assert.eq(0, exitCode, "mongerrestore failed to restore the oplog");

    var count = restoreMongerd.getDB("local").getCollection("oplog.rs").count();
    if (count != 20) {
        print("mongerrestore restored too many documents");
        restoreMongerd.getDB("local").getCollection("oplog.rs").find().pretty().shellPrint();
        assert.eq(count, 20, "mongerrestore should only have inserted the latter 20 entries");
    }

    MongerRunner.stopMongerd(restoreMongerd);
    step("stopping replset test");
    replTest.stopSet();
})();
