(function() {
    "use strict";
    var t = db.create_indexes_shell_helper;
    t.drop();

    var monger = db.getMongo();

    try {
        var commandsRan = [];
        var insertsRan = [];
        var mockMongo = {
            writeMode: function() {
                return "commands";
            },
            getSlaveOk: function() {
                return true;
            },
            runCommand: function(db, cmd, opts) {
                commandsRan.push({db: db, cmd: cmd, opts: opts});
                return {ok: 1.0};
            },
            getWriteConcern: function() {
                return null;
            },
            useWriteCommands: function() {
                return true;
            },
            hasWriteCommands: function() {
                return true;
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
            isMongos: function() {
                return monger.isMongos();
            },
            isCausalConsistency: function() {
                return false;
            },
            getClusterTime: function() {
                return null;
            },
        };

        db._monger = mockMongo;
        db._session = new _DummyDriverSession(mockMongo);

        t.createIndexes([{x: 1}]);
        assert.eq(commandsRan.length, 1);
        assert(commandsRan[0].cmd.hasOwnProperty("createIndexes"));
        assert.eq(commandsRan[0].cmd["indexes"][0], {key: {x: 1}, name: "x_1"});

        commandsRan = [];

        t.createIndexes([{y: 1}, {z: -1}]);
        assert.eq(commandsRan.length, 1);
        assert(commandsRan[0].cmd.hasOwnProperty("createIndexes"));
        assert.eq(commandsRan[0].cmd["indexes"][0], {key: {y: 1}, name: "y_1"});
        assert.eq(commandsRan[0].cmd["indexes"][1], {key: {z: -1}, name: "z_-1"});

        commandsRan = [];

        t.createIndex({a: 1});
        assert.eq(commandsRan.length, 1);
        assert(commandsRan[0].cmd.hasOwnProperty("createIndexes"));
        assert.eq(commandsRan[0].cmd["indexes"][0], {key: {a: 1}, name: "a_1"});
    } finally {
        db._monger = monger;
        db._session = new _DummyDriverSession(monger);
    }
}());
