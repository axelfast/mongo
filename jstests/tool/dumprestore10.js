// simple test to ensure write concern functions as expected
(function() {
    // Skip this test if running with --nojournal and WiredTiger.
    if (jsTest.options().noJournal &&
        (!jsTest.options().storageEngine || jsTest.options().storageEngine === "wiredTiger")) {
        print("Skipping test because running WiredTiger without journaling isn't a valid" +
              " replica set configuration");
        return;
    }

    var name = "dumprestore10";

    function step(msg) {
        msg = msg || "";
        this.x = (this.x || 0) + 1;
        print('\n' + name + ".js step " + this.x + ' ' + msg);
    }

    step();

    var replTest = new ReplSetTest({name: name, nodes: 2});
    var nodes = replTest.startSet();
    replTest.initiate();
    var master = replTest.getPrimary();
    var total = 1000;

    {
        step("store data");
        var foo = master.getDB("foo");
        for (i = 0; i < total; i++) {
            foo.bar.insert({x: i, y: "abc"});
        }
    }

    {
        step("wait");
        replTest.awaitReplication();
    }

    step("mongerdump from replset");

    var data = MongerRunner.dataDir + "/dumprestore10-dump1/";

    var exitCode = MongerRunner.runMongerTool("mongerdump", {
        host: "127.0.0.1:" + master.port,
        out: data,
    });
    assert.eq(0, exitCode, "mongerdump failed to dump data from the replica set");

    {
        step("remove data after dumping");
        master.getDB("foo").getCollection("bar").drop();
    }

    {
        step("wait");
        replTest.awaitReplication();
    }

    step("try mongerrestore with write concern");

    exitCode = MongerRunner.runMongerTool("mongerrestore", {
        writeConcern: "2",
        host: "127.0.0.1:" + master.port,
        dir: data,
    });
    assert.eq(0,
              exitCode,
              "mongerrestore failed to restore the data to a replica set while using w=2 writes");

    var x = 0;

    // no waiting for replication
    x = master.getDB("foo").getCollection("bar").count();

    assert.eq(x, total, "mongerrestore should have successfully restored the collection");

    step("stopSet");
    replTest.stopSet();

    step("SUCCESS");
}());
