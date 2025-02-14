// Confirms that profiled insert execution contains all expected metrics with proper values.
//
// @tags: [
//   assumes_write_concern_unchanged,
//   does_not_support_stepdowns,
//   requires_profiling,
// ]

(function() {
    "use strict";

    // For getLatestProfilerEntry and getProfilerProtocolStringForCommand
    load("jstests/libs/profiler.js");

    var testDB = db.getSiblingDB("profile_insert");
    assert.commandWorked(testDB.dropDatabase());
    var coll = testDB.getCollection("test");
    var isWriteCommand = (db.getMonger().writeMode() === "commands");

    testDB.setProfilingLevel(2);

    //
    // Test single insert.
    //
    var doc = {_id: 1};
    var result = coll.insert(doc);
    if (isWriteCommand) {
        assert.writeOK(result);
    }

    var profileObj = getLatestProfilerEntry(testDB);

    assert.eq(profileObj.ns, coll.getFullName(), tojson(profileObj));
    assert.eq(profileObj.op, "insert", tojson(profileObj));
    assert.eq(profileObj.ninserted, 1, tojson(profileObj));
    assert.eq(profileObj.keysInserted, 1, tojson(profileObj));
    if (isWriteCommand) {
        assert.eq(profileObj.command.ordered, true, tojson(profileObj));
        assert.eq(profileObj.protocol,
                  getProfilerProtocolStringForCommand(testDB.getMonger()),
                  tojson(profileObj));
        assert(profileObj.hasOwnProperty("responseLength"), tojson(profileObj));
    }

    assert(profileObj.hasOwnProperty("numYield"), tojson(profileObj));
    assert(profileObj.hasOwnProperty("locks"), tojson(profileObj));
    assert(profileObj.hasOwnProperty("millis"), tojson(profileObj));
    assert(profileObj.hasOwnProperty("ts"), tojson(profileObj));
    assert(profileObj.hasOwnProperty("client"), tojson(profileObj));
    assert.eq(profileObj.appName, "MongerDB Shell", tojson(profileObj));

    //
    // Test multi-insert.
    //
    coll.drop();

    var docArray = [{_id: 1}, {_id: 2}];
    var bulk = coll.initializeUnorderedBulkOp();
    bulk.insert(docArray[0]);
    bulk.insert(docArray[1]);
    result = bulk.execute();
    if (isWriteCommand) {
        assert.writeOK(result);
    }

    profileObj = getLatestProfilerEntry(testDB);

    if (isWriteCommand) {
        assert.eq(profileObj.ninserted, 2, tojson(profileObj));
        assert.eq(profileObj.keysInserted, 2, tojson(profileObj));
        assert.eq(profileObj.appName, "MongerDB Shell", tojson(profileObj));
    } else {
        // Documents were inserted one at a time.
        assert.eq(profileObj.ninserted, 1, tojson(profileObj));
        assert.eq(profileObj.keysInserted, 1, tojson(profileObj));
        assert.eq(profileObj.appName, "MongerDB Shell", tojson(profileObj));
    }

    //
    // Test insert options.
    //
    coll.drop();
    doc = {_id: 1};
    var wtimeout = 60000;
    assert.writeOK(coll.insert(doc, {writeConcern: {w: 1, wtimeout: wtimeout}, ordered: false}));

    profileObj = getLatestProfilerEntry(testDB);

    if (isWriteCommand) {
        assert.eq(profileObj.command.ordered, false, tojson(profileObj));
        assert.eq(profileObj.command.writeConcern.w, 1, tojson(profileObj));
        assert.eq(profileObj.command.writeConcern.wtimeout, wtimeout, tojson(profileObj));
        assert.eq(profileObj.appName, "MongerDB Shell", tojson(profileObj));
    }
})();
