// Tests that the read preference set on the connection is used when we call the count helper.
(function() {
    "use strict";

    var commandsRan = [];

    // Create a new DB object backed by a mock connection.
    function MockMonger() {
        this.getMinWireVersion = function getMinWireVersion() {
            return 0;
        };

        this.getMaxWireVersion = function getMaxWireVersion() {
            return 0;
        };
    }
    MockMonger.prototype = Monger.prototype;
    MockMonger.prototype.runCommand = function(db, cmd, opts) {
        commandsRan.push({db: db, cmd: cmd, opts: opts});
        return {ok: 1, n: 100};
    };

    const mockMonger = new MockMonger();
    var db = new DB(mockMonger, "test");

    // Attach a dummy implicit session because the mock connection cannot create sessions.
    db._session = new _DummyDriverSession(mockMonger);

    assert.eq(commandsRan.length, 0);

    // Run a count with no readPref.
    db.getMonger().setReadPref(null);
    db.foo.count();

    // Check that there is no readPref on the command document.
    assert.eq(commandsRan.length, 1);
    assert.docEq(commandsRan[0].cmd, {count: "foo", query: {}});

    commandsRan = [];

    // Run with readPref secondary.
    db.getMonger().setReadPref("secondary");
    db.foo.count();

    // Check that we have wrapped the command and attached the read preference.
    assert.eq(commandsRan.length, 1);
    assert.docEq(commandsRan[0].cmd,
                 {query: {count: "foo", query: {}}, $readPreference: {mode: "secondary"}});

})();
