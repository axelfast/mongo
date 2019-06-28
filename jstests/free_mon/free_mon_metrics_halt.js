// Ensure free monitoring gives up if metrics returns halt
//
load("jstests/free_mon/libs/free_mon.js");

(function() {
    'use strict';

    let mock_web = new FreeMonWebServer(FAULT_HALT_METRICS_5);

    mock_web.start();

    let options = {
        setParameter: "cloudFreeMonitoringEndpointURL=" + mock_web.getURL(),
        enableFreeMonitoring: "on",
        verbose: 1,
    };

    const conn = MongerRunner.runMongerd(options);
    assert.neq(null, conn, 'mongerd was unable to start up');

    mock_web.waitMetrics(6);

    // It gets marked as disabled on halt
    const reg = FreeMonGetRegistration(conn);
    print(tojson(reg));
    assert.eq(reg.state, "disabled");

    MongerRunner.stopMongerd(conn);

    mock_web.stop();
})();
