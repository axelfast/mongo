// Attempt to shut the server down as it is initializing replication, and confirm it doesn't crash.
//
load('jstests/replsets/rslib.js');
(function() {
    "use strict";

    let ns = "test.coll";

    let rst = new ReplSetTest({
        nodes: 2,
    });

    let conf = rst.getReplSetConfig();
    conf.members[1].votes = 0;
    conf.members[1].priority = 0;
    conf.members[1].hidden = true;

    rst.startSet();
    rst.initiate(conf);
    rst.awaitReplication();

    let secondary = rst.getSecondary();
    rst.stop(secondary);
    let program = rst.start(
        secondary,
        {waitForConnect: false, setParameter: "failpoint.shutdownAtStartup={mode:'alwaysOn'}"});
    // mongerd should exit automatically, since failpoint was set.
    let exitCode = waitProgram(program.pid);
    assert.eq(0, exitCode);
    rst.stopSet();
})();
