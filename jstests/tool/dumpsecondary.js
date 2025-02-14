(function() {
    // Skip this test if running with --nojournal and WiredTiger.
    if (jsTest.options().noJournal &&
        (!jsTest.options().storageEngine || jsTest.options().storageEngine === "wiredTiger")) {
        print("Skipping test because running WiredTiger without journaling isn't a valid" +
              " replica set configuration");
        return;
    }

    var replTest = new ReplSetTest({name: 'testSet', nodes: 2});

    var nodes = replTest.startSet();
    replTest.initiate();

    var master = replTest.getPrimary();
    db = master.getDB("foo");
    db.foo.save({a: 1000});
    replTest.awaitReplication();
    replTest.awaitSecondaryNodes();

    assert.eq(1, db.foo.count(), "setup");

    var slaves = replTest._slaves;
    assert(slaves.length == 1, "Expected 1 slave but length was " + slaves.length);
    slave = slaves[0];

    var commonOptions = {};
    if (jsTest.options().keyFile) {
        commonOptions.username = jsTest.options().authUser;
        commonOptions.password = jsTest.options().authPassword;
    }

    var exitCode = MongerRunner.runMongerTool(
        "mongerdump",
        Object.extend({
            host: slave.host,
            out: MongerRunner.dataDir + "/jstests_tool_dumpsecondary_external/",
        },
                      commonOptions));
    assert.eq(0, exitCode, "mongerdump failed to dump data from the secondary");

    db.foo.drop();
    assert.eq(0, db.foo.count(), "after drop");

    exitCode = MongerRunner.runMongerTool(
        "mongerrestore",
        Object.extend({
            host: master.host,
            dir: MongerRunner.dataDir + "/jstests_tool_dumpsecondary_external/",
        },
                      commonOptions));
    assert.eq(0, exitCode, "mongerrestore failed to restore data to the primary");

    assert.soon("db.foo.findOne()", "no data after sleep");
    assert.eq(1, db.foo.count(), "after restore");
    assert.eq(1000, db.foo.findOne().a, "after restore 2");

    resetDbpath(MongerRunner.dataDir + '/jstests_tool_dumpsecondary_external');

    replTest.stopSet(15);
}());
