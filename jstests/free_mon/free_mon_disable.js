// Validate disable works
//
load("jstests/free_mon/libs/free_mon.js");

(function() {
    'use strict';

    let mock_web = new FreeMonWebServer();

    mock_web.start();

    let options = {
        setParameter: "cloudFreeMonitoringEndpointURL=" + mock_web.getURL(),
        freeMonitoringTag: "foo",
        verbose: 1,
    };

    const conn = MongerRunner.runMongerd(options);
    assert.neq(null, conn, 'mongerd was unable to start up');

    assert.commandWorked(conn.adminCommand({setFreeMonitoring: 1, action: "disable"}));

    const stats = mock_web.queryStats();
    print(tojson(stats));

    assert.eq(stats.registers, 0);

    assert.eq(FreeMonGetStatus(conn).state, "disabled");

    assert.eq(FreeMonGetServerStatus(conn).state, "disabled");

    MongerRunner.stopMongerd(conn);

    mock_web.stop();
})();
