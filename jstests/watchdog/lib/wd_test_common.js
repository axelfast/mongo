// Storage Node Watchdog common test code
//
load("jstests/watchdog/lib/charybdefs_lib.js");

function testMongerDHang(control, mongerd_options) {
    'use strict';

    // Now start MongerD with it enabled at startup
    //
    if (mongerd_options.hasOwnProperty("dbPath")) {
        resetDbpath(mongerd_options.dbPath);
    }

    var options = {
        setParameter: "watchdogPeriodSeconds=" + control.getWatchdogPeriodSeconds(),
        verbose: 1,
    };

    options = Object.extend(mongerd_options, options);

    const conn = MongerRunner.runMongerd(options);
    assert.neq(null, conn, 'mongerd was unable to start up');

    // Wait for watchdog to get running
    const admin = conn.getDB("admin");

    // Wait for the watchdog to run some checks first
    control.waitForWatchdogToStart(admin);

    // Hang the file system
    control.addWriteDelayFaultAndWait("watchdog_probe.*");

    // Check MongerD is dead by sending SIGTERM
    // This will trigger our "nice" shutdown, but since mongerd is stuck in the kernel doing I/O,
    // the process will not terminate until charybdefs is done sleeping.
    print("Stopping MongerDB now, it will terminate once charybdefs is done sleeping.");
    MongerRunner.stopMongerd(conn, undefined, {allowedExitCode: EXIT_WATCHDOG});
}

function testFuseAndMongerD(control, mongerd_options) {
    'use strict';

    // Cleanup previous runs
    control.cleanup();

    try {
        // Start the file system
        control.start();

        testMongerDHang(control, mongerd_options);
    } finally {
        control.cleanup();
    }
}
