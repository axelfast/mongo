/**
 * Ensures that the dropDatabase shell helper accepts a write concern as its first argument.
 *
 * @tags: [requires_replication]
 */
(function() {
    "use strict";

    function checkWriteConcern(testFn, checkFn) {
        const mongerRunCommandOriginal = Monger.prototype.runCommand;

        const sentinel = {};
        let cmdObjSeen = sentinel;

        Monger.prototype.runCommand = function runCommandSpy(dbName, cmdObj, options) {
            cmdObjSeen = cmdObj;
            return mongerRunCommandOriginal.apply(this, arguments);
        };

        try {
            assert.doesNotThrow(testFn);
        } finally {
            Monger.prototype.runCommand = mongerRunCommandOriginal;
        }

        if (cmdObjSeen == sentinel) {
            throw new Error("Monger.prototype.runCommand() was never called: " + testFn.toString());
        }

        checkFn(cmdObjSeen);
    }

    const rst = new ReplSetTest({nodes: 2});
    rst.startSet();
    rst.initiate();

    const dbName = "dbDrop";
    const collName = "coll";
    const primaryDB = rst.getPrimary().getDB(dbName);

    primaryDB.createCollection(collName);
    checkWriteConcern(() => assert.commandWorked(primaryDB.dropDatabase({w: "majority"})),
                      (cmdObj) => {
                          assert.eq(cmdObj.writeConcern, {w: "majority"});
                      });

    primaryDB.createCollection(collName);
    checkWriteConcern(() => assert.commandWorked(primaryDB.dropDatabase({w: 1})), (cmdObj) => {
        assert.eq(cmdObj.writeConcern, {w: 1});
    });

    primaryDB.createCollection(collName);
    checkWriteConcern(() => assert.commandFailedWithCode(primaryDB.dropDatabase({w: 100000}),
                                                         ErrorCodes.UnsatisfiableWriteConcern),
                      (cmdObj) => {
                          assert.eq(cmdObj.writeConcern, {w: 100000});
                      });

    rst.stopSet();
})();
