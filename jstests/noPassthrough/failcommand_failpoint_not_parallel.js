(function() {
    "use strict";

    const conn = MongerRunner.runMongerd();
    assert.neq(null, conn);
    const db = conn.getDB("test_failcommand_noparallel");

    // Test times when closing connection.
    assert.commandWorked(db.adminCommand({
        configureFailPoint: "failCommand",
        mode: {times: 2},
        data: {
            closeConnection: true,
            failCommands: ["find"],
        }
    }));
    assert.throws(() => db.runCommand({find: "c"}));
    assert.throws(() => db.runCommand({find: "c"}));
    assert.commandWorked(db.runCommand({find: "c"}));
    assert.commandWorked(db.adminCommand({configureFailPoint: "failCommand", mode: "off"}));

    MongerRunner.stopMongerd(conn);

}());
