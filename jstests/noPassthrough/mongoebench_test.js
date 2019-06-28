/**
 * Tests for the mongerebench executable.
 */
(function() {
    "use strict";

    load("jstests/libs/mongerebench.js");  // for runMongereBench

    if (jsTest.options().storageEngine !== "mobile") {
        print("Skipping test because storage engine isn't mobile");
        return;
    }

    const dbpath = MongerRunner.dataPath + "mongerebench_test";
    resetDbpath(dbpath);

    // Test that the operations in the "pre" section of the configuration are run exactly once.
    runMongereBench(  // Force clang-format to break this line.
        {
          pre: [{
              op: "insert",
              ns: "test.mongerebench_test",
              doc: {pre: {"#SEQ_INT": {seq_id: 0, start: 0, step: 1, unique: true}}}
          }],
          ops: [{
              op: "update",
              ns: "test.mongerebench_test",
              update: {$inc: {ops: 1}},
              multi: true,
          }]
        },
        {dbpath});

    const output = cat(dbpath + "/perf.json");
    const stats = assert.doesNotThrow(
        JSON.parse, [output], "failed to parse output file as strict JSON: " + output);
    assert.eq({$numberLong: "0"},
              stats.errCount,
              () => "stats file reports errors but exit code was zero: " + tojson(stats));
    assert(stats.hasOwnProperty("totalOps/s"),
           () => "stats file doesn't report ops per second: " + tojson(stats));

    const conn = MongerRunner.runMongerd({dbpath, noCleanData: true});
    assert.neq(null, conn, "failed to start mongerd after running mongerebench");

    const db = conn.getDB("test");
    const count = db.mongerebench_test.find().itcount();
    assert.eq(1, count, "ops in 'pre' section ran more than once or didn't run at all");

    MongerRunner.stopMongerd(conn);
})();
