/**
 * Ensure that we allow mongerd to shutdown cleanly while being fsync locked.
 */
(function() {
    "use strict";

    let conn = MongerRunner.runMongerd();
    let db = conn.getDB("test");

    for (let i = 0; i < 10; i++) {
        assert.commandWorked(db.adminCommand({fsync: 1, lock: 1}));
    }

    MongerRunner.stopMongerd(conn, MongerRunner.EXIT_CLEAN, {skipValidation: true});
}());
