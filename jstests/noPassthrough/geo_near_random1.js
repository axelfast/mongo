// this tests all points using $near
var db;
(function() {
    "use strict";
    load("jstests/libs/geo_near_random.js");

    const conn = MongerRunner.runMongerd();
    assert.neq(null, conn, "mongerd failed to start.");
    db = conn.getDB("test");

    var test = new GeoNearRandomTest("weekly.geo_near_random1");

    test.insertPts(1000);

    test.testPt([0, 0]);
    test.testPt(test.mkPt());
    test.testPt(test.mkPt());
    test.testPt(test.mkPt());
    test.testPt(test.mkPt());

    MongerRunner.stopMongerd(conn);
})();
