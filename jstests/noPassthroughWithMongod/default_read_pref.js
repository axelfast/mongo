// Tests that the default read preference is 'unset', and that the slaveOk bit is not set
// on read commands run with an 'unset' read preference.
(function() {

    "use strict";

    var monger = db.getMonger();
    try {
        var commandsRan = [];
        db._monger = {
            getSlaveOk: function() {
                return false;
            },
            getReadPrefMode: function() {
                return monger.getReadPrefMode();
            },
            getReadPref: function() {
                return monger.getReadPref();
            },
            runCommand: function(db, cmd, opts) {
                commandsRan.push({db: db, cmd: cmd, opts: opts});
                return {ok: 1};
            },
            getMinWireVersion: function() {
                return monger.getMinWireVersion();
            },
            getMaxWireVersion: function() {
                return monger.getMaxWireVersion();
            },
            isReplicaSetMember: function() {
                return monger.isReplicaSetMember();
            },
            isMongers: function() {
                return monger.isMongers();
            },
            isCausalConsistency: function() {
                return false;
            },
            getClusterTime: function() {
                return null;
            },
        };
        db._session = new _DummyDriverSession(db._monger);

        db.runReadCommand({ping: 1});
        assert.eq(commandsRan.length, 1);
        assert.docEq(commandsRan[0].cmd, {ping: 1}, "The command should not have been wrapped.");
        assert.eq(
            commandsRan[0].opts & DBQuery.Option.slaveOk, 0, "The slaveOk bit should not be set.");

    } finally {
        db._monger = monger;
        db._session = new _DummyDriverSession(monger);
    }

})();
