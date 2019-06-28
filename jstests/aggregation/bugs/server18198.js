// SERVER-18198 check read pref is only applied when there is no $out stage
// in aggregate shell helper
(function() {
    "use strict";
    var t = db.server18198;
    t.drop();

    var monger = db.getMonger();

    try {
        var commandsRan = [];
        // hook in our patched monger
        var mockMonger = {
            getSlaveOk: function() {
                return true;
            },
            runCommand: function(db, cmd, opts) {
                commandsRan.push({db: db, cmd: cmd, opts: opts});
                return {ok: 1.0};
            },
            getReadPref: function() {
                return {mode: "secondaryPreferred"};
            },
            getReadPrefMode: function() {
                return "secondaryPreferred";
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
                return monger.getClusterTime();
            },
        };

        db._monger = mockMonger;
        db._session = new _DummyDriverSession(mockMonger);

        // this query should not get a read pref
        t.aggregate([{$sort: {"x": 1}}, {$out: "foo"}]);
        assert.eq(commandsRan.length, 1);
        // check that it doesn't have a read preference
        assert(!commandsRan[0].cmd.hasOwnProperty("$readPreference"));

        commandsRan = [];

        t.aggregate([{$sort: {"x": 1}}]);
        // check another command was run
        assert.eq(commandsRan.length, 1);
        // check that it has a read preference
        assert(commandsRan[0].cmd.hasOwnProperty("$readPreference"));
    } finally {
        db._monger = monger;
        db._session = new _DummyDriverSession(monger);
    }
})();
