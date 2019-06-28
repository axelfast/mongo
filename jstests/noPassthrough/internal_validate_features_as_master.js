// Tests the internalValidateFeaturesAsMaster server parameter.

(function() {
    "use strict";

    load("jstests/libs/get_index_helpers.js");

    // internalValidateFeaturesAsMaster can be set via startup parameter.
    let conn = MongerRunner.runMongerd({setParameter: "internalValidateFeaturesAsMaster=1"});
    assert.neq(null, conn, "mongerd was unable to start up");
    let res = conn.adminCommand({getParameter: 1, internalValidateFeaturesAsMaster: 1});
    assert.commandWorked(res);
    assert.eq(res.internalValidateFeaturesAsMaster, true);
    MongerRunner.stopMongerd(conn);

    // internalValidateFeaturesAsMaster cannot be set with --replSet.
    conn = MongerRunner.runMongerd(
        {replSet: "replSetName", setParameter: "internalValidateFeaturesAsMaster=0"});
    assert.eq(null, conn, "mongerd was unexpectedly able to start up");

    conn = MongerRunner.runMongerd(
        {replSet: "replSetName", setParameter: "internalValidateFeaturesAsMaster=1"});
    assert.eq(null, conn, "mongerd was unexpectedly able to start up");

    // internalValidateFeaturesAsMaster cannot be set via runtime parameter.
    conn = MongerRunner.runMongerd({});
    assert.commandFailed(
        conn.adminCommand({setParameter: 1, internalValidateFeaturesAsMaster: true}));
    assert.commandFailed(
        conn.adminCommand({setParameter: 1, internalValidateFeaturesAsMaster: false}));
    MongerRunner.stopMongerd(conn);
}());
