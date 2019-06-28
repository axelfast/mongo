// Ensure free monitoring gives up if metrics returns permanently delete
//
load("jstests/free_mon/libs/free_mon.js");

(function() {
    'use strict';

    let mock_web = new FreeMonWebServer(FAULT_PERMANENTLY_DELETE_AFTER_3);

    mock_web.start();

    let options = {
        setParameter: "cloudFreeMonitoringEndpointURL=" + mock_web.getURL(),
        enableFreeMonitoring: "on",
        verbose: 1,
    };

    const conn = MongerRunner.runMongerd(options);
    assert.neq(null, conn, 'mongerd was unable to start up');

    mock_web.waitMetrics(4);

    // Make sure the registration document gets removed
    const reg = FreeMonGetRegistration(conn);
    print(tojson(reg));
    assert.eq(reg, undefined);

    MongerRunner.stopMongerd(conn);

    mock_web.stop();
})();
