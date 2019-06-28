/*
 * SERVER-28990: Tests that a mongerd started with --repair doesn't attempt binding to a port.
 */

(function() {
    "use strict";
    let dbpath = MongerRunner.dataPath + "repair_flag_transport_layer";
    resetDbpath(dbpath);

    function runTest(conn) {
        let returnCode =
            runNonMongerProgram("mongerd", "--port", conn.port, "--repair", "--dbpath", dbpath);
        assert.eq(
            returnCode, 0, "expected mongerd --repair to execute successfully regardless of port");
    }

    let conn = MongerRunner.runMongerd();

    runTest(conn);
    MongerRunner.stopMongerd(conn);

})();
