/**
 * Tests that failpoints can be set via --setParameter on the command line for mongers and mongerd
 * only when running with enableTestCommands=1.
 */
(function() {

    "use strict";

    var assertStartupSucceeds = function(conn) {
        assert.commandWorked(conn.adminCommand({ismaster: 1}));
    };

    var assertStartupFails = function(conn) {
        assert.eq(null, conn);
    };

    var validFailpointPayload = {'mode': 'alwaysOn'};
    var validFailpointPayloadWithData = {'mode': 'alwaysOn', 'data': {x: 1}};
    var invalidFailpointPayload = "notJSON";

    // In order to be able connect to a mongers that starts up successfully, start a config replica
    // set so that we can provide a valid config connection string to the mongers.
    var configRS = new ReplSetTest({nodes: 3});
    configRS.startSet({configsvr: '', storageEngine: 'wiredTiger'});
    configRS.initiate();

    // Setting a failpoint via --setParameter fails if enableTestCommands is not on.
    jsTest.setOption('enableTestCommands', false);
    assertStartupFails(
        MongerRunner.runMongerd({setParameter: "failpoint.dummy=" + tojson(validFailpointPayload)}));
    assertStartupFails(MongerRunner.runMongers({
        setParameter: "failpoint.dummy=" + tojson(validFailpointPayload),
        configdb: configRS.getURL()
    }));
    jsTest.setOption('enableTestCommands', true);

    // Passing an invalid failpoint payload fails.
    assertStartupFails(MongerRunner.runMongerd(
        {setParameter: "failpoint.dummy=" + tojson(invalidFailpointPayload)}));
    assertStartupFails(MongerRunner.runMongers({
        setParameter: "failpoint.dummy=" + tojson(invalidFailpointPayload),
        configdb: configRS.getURL()
    }));

    // Valid startup configurations succeed.
    var mongerd =
        MongerRunner.runMongerd({setParameter: "failpoint.dummy=" + tojson(validFailpointPayload)});
    assertStartupSucceeds(mongerd);
    MongerRunner.stopMongerd(mongerd);

    var mongers = MongerRunner.runMongers({
        setParameter: "failpoint.dummy=" + tojson(validFailpointPayload),
        configdb: configRS.getURL()
    });
    assertStartupSucceeds(mongers);
    MongerRunner.stopMongers(mongers);

    mongerd = MongerRunner.runMongerd(
        {setParameter: "failpoint.dummy=" + tojson(validFailpointPayloadWithData)});
    assertStartupSucceeds(mongerd);

    mongers = MongerRunner.runMongers({
        setParameter: "failpoint.dummy=" + tojson(validFailpointPayloadWithData),
        configdb: configRS.getURL()
    });
    assertStartupSucceeds(mongers);

    // The failpoint shows up with the correct data in the results of getParameter.

    var res = mongerd.adminCommand({getParameter: "*"});
    assert.neq(null, res);
    assert.neq(null, res["failpoint.dummy"]);
    assert.eq(1, res["failpoint.dummy"].mode);  // the 'mode' is an enum internally; 'alwaysOn' is 1
    assert.eq(validFailpointPayloadWithData.data, res["failpoint.dummy"].data);

    res = mongers.adminCommand({getParameter: "*"});
    assert.neq(null, res);
    assert.neq(null, res["failpoint.dummy"]);
    assert.eq(1, res["failpoint.dummy"].mode);  // the 'mode' is an enum internally; 'alwaysOn' is 1
    assert.eq(validFailpointPayloadWithData.data, res["failpoint.dummy"].data);

    // The failpoint cannot be set by the setParameter command.
    assert.commandFailed(mongerd.adminCommand({setParameter: 1, "dummy": validFailpointPayload}));
    assert.commandFailed(mongers.adminCommand({setParameter: 1, "dummy": validFailpointPayload}));

    // After changing the failpoint's state through the configureFailPoint command, the changes are
    // reflected in the output of the getParameter command.

    var newData = {x: 2};

    mongerd.adminCommand({configureFailPoint: "dummy", mode: "alwaysOn", data: newData});
    res = mongerd.adminCommand({getParameter: 1, "failpoint.dummy": 1});
    assert.neq(null, res);
    assert.neq(null, res["failpoint.dummy"]);
    assert.eq(1, res["failpoint.dummy"].mode);  // the 'mode' is an enum internally; 'alwaysOn' is 1
    assert.eq(newData, res["failpoint.dummy"].data);

    mongers.adminCommand({configureFailPoint: "dummy", mode: "alwaysOn", data: newData});
    res = mongers.adminCommand({getParameter: 1, "failpoint.dummy": 1});
    assert.neq(null, res);
    assert.neq(null, res["failpoint.dummy"]);
    assert.eq(1, res["failpoint.dummy"].mode);  // the 'mode' is an enum internally; 'alwaysOn' is 1
    assert.eq(newData, res["failpoint.dummy"].data);

    MongerRunner.stopMongerd(mongerd);
    MongerRunner.stopMongers(mongers);

    // Failpoint server parameters do not show up in the output of getParameter when not running
    // with enableTestCommands=1.

    jsTest.setOption('enableTestCommands', false);
    TestData.roleGraphInvalidationIsFatal = false;

    mongerd = MongerRunner.runMongerd();
    assertStartupSucceeds(mongerd);

    mongers = MongerRunner.runMongers({configdb: configRS.getURL()});
    assertStartupSucceeds(mongers);

    // Doing getParameter for a specific failpoint fails.
    assert.commandFailed(mongerd.adminCommand({getParameter: 1, "failpoint.dummy": 1}));
    assert.commandFailed(mongers.adminCommand({getParameter: 1, "failpoint.dummy": 1}));

    // No failpoint parameters show up when listing all parameters through getParameter.
    res = mongerd.adminCommand({getParameter: "*"});
    assert.neq(null, res);
    for (var parameter in res) {  // for-in loop valid only for top-level field checks.
        assert(!parameter.includes("failpoint."));
    }

    res = mongers.adminCommand({getParameter: "*"});
    assert.neq(null, res);
    for (var parameter in res) {  // for-in loop valid only for top-level field checks.
        assert(!parameter.includes("failpoint."));
    }

    MongerRunner.stopMongerd(mongerd);
    MongerRunner.stopMongers(mongers);
    configRS.stopSet();
})();
