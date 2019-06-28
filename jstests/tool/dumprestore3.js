// mongerdump/mongerexport from primary should succeed.  mongerrestore and mongerimport to a
// secondary node should fail.

(function() {
    // Skip this test if running with --nojournal and WiredTiger.
    if (jsTest.options().noJournal &&
        (!jsTest.options().storageEngine || jsTest.options().storageEngine === "wiredTiger")) {
        print("Skipping test because running WiredTiger without journaling isn't a valid" +
              " replica set configuration");
        return;
    }

    var name = "dumprestore3";

    var replTest = new ReplSetTest({name: name, nodes: 2});
    var nodes = replTest.startSet();
    replTest.initiate();
    var primary = replTest.getPrimary();
    var secondary = replTest.getSecondary();

    jsTestLog("populate primary");
    var foo = primary.getDB("foo");
    for (i = 0; i < 20; i++) {
        foo.bar.insert({x: i, y: "abc"});
    }

    jsTestLog("wait for secondary");
    replTest.awaitReplication();

    jsTestLog("mongerdump from primary");
    var data = MongerRunner.dataDir + "/dumprestore3-other1/";
    resetDbpath(data);
    var ret = MongerRunner.runMongerTool("mongerdump", {
        host: primary.host,
        out: data,
    });
    assert.eq(ret, 0, "mongerdump should exit w/ 0 on primary");

    jsTestLog("try mongerrestore to secondary");
    ret = MongerRunner.runMongerTool("mongerrestore", {
        host: secondary.host,
        dir: data,
    });
    assert.neq(ret, 0, "mongerrestore should exit w/ 1 on secondary");

    jsTestLog("mongerexport from primary");
    dataFile = MongerRunner.dataDir + "/dumprestore3-other2.json";
    ret = MongerRunner.runMongerTool("mongerexport", {
        host: primary.host,
        out: dataFile,
        db: "foo",
        collection: "bar",
    });
    assert.eq(ret, 0, "mongerexport should exit w/ 0 on primary");

    jsTestLog("mongerimport from secondary");
    ret = MongerRunner.runMongerTool("mongerimport", {
        host: secondary.host,
        file: dataFile,
    });
    assert.neq(ret, 0, "mongerimport should exit w/ 1 on secondary");

    jsTestLog("stopSet");
    replTest.stopSet();
    jsTestLog("SUCCESS");
}());
