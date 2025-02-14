/**
 * Verifies behavior around implicit sessions in the monger shell.
 */
(function() {
    "use strict";

    /**
     * Runs the given function, inspecting the outgoing command object and making assertions about
     * its logical session id.
     */
    function inspectCommandForSessionId(func, {shouldIncludeId, expectedId, differentFromId}) {
        const mongerRunCommandOriginal = Monger.prototype.runCommand;

        const sentinel = {};
        let cmdObjSeen = sentinel;

        Monger.prototype.runCommand = function runCommandSpy(dbName, cmdObj, options) {
            cmdObjSeen = cmdObj;
            return mongerRunCommandOriginal.apply(this, arguments);
        };

        try {
            assert.doesNotThrow(func);
        } finally {
            Monger.prototype.runCommand = mongerRunCommandOriginal;
        }

        if (cmdObjSeen === sentinel) {
            throw new Error("Monger.prototype.runCommand() was never called: " + func.toString());
        }

        // If the command is in a wrapped form, then we look for the actual command object inside
        // the query/$query object.
        let cmdName = Object.keys(cmdObjSeen)[0];
        if (cmdName === "query" || cmdName === "$query") {
            cmdObjSeen = cmdObjSeen[cmdName];
            cmdName = Object.keys(cmdObjSeen)[0];
        }

        if (shouldIncludeId) {
            assert(cmdObjSeen.hasOwnProperty("lsid"),
                   "Expected operation " + tojson(cmdObjSeen) + " to have a logical session id.");

            if (expectedId) {
                assert(bsonBinaryEqual(expectedId, cmdObjSeen.lsid),
                       "The sent session id did not match the expected, sent: " +
                           tojson(cmdObjSeen.lsid) + ", expected: " + tojson(expectedId));
            }

            if (differentFromId) {
                assert(!bsonBinaryEqual(differentFromId, cmdObjSeen.lsid),
                       "The sent session id was not different from the expected, sent: " +
                           tojson(cmdObjSeen.lsid) + ", expected: " + tojson(differentFromId));
            }

        } else {
            assert(
                !cmdObjSeen.hasOwnProperty("lsid"),
                "Expected operation " + tojson(cmdObjSeen) + " to not have a logical session id.");
        }

        return cmdObjSeen.lsid;
    }

    // Tests regular behavior of implicit sessions.
    function runTest() {
        const conn = MongerRunner.runMongerd();

        // Commands run on a database without an explicit session should use an implicit one.
        const testDB = conn.getDB("test");
        const coll = testDB.getCollection("foo");
        const implicitId = inspectCommandForSessionId(function() {
            assert.writeOK(coll.insert({x: 1}));
        }, {shouldIncludeId: true});

        // Unacknowledged writes have no session id.
        inspectCommandForSessionId(function() {
            coll.insert({x: 1}, {writeConcern: {w: 0}});
        }, {shouldIncludeId: false});

        assert(bsonBinaryEqual(testDB.getSession().getSessionId(), implicitId),
               "Expected the id of the database's implicit session to match the one sent, sent: " +
                   tojson(implicitId) + " db session id: " +
                   tojson(testDB.getSession().getSessionId()));

        // Implicit sessions are not causally consistent.
        assert(!testDB.getSession().getOptions().isCausalConsistency(),
               "Expected the database's implicit session to not be causally consistent");

        // Further commands run on the same database should reuse the implicit session.
        inspectCommandForSessionId(function() {
            assert.writeOK(coll.insert({x: 1}));
        }, {shouldIncludeId: true, expectedId: implicitId});

        // New collections from the same database should inherit the implicit session.
        const collTwo = testDB.getCollection("bar");
        inspectCommandForSessionId(function() {
            assert.writeOK(collTwo.insert({x: 1}));
        }, {shouldIncludeId: true, expectedId: implicitId});

        // Sibling databases should inherit the implicit session.
        let siblingColl = testDB.getSiblingDB("foo").getCollection("bar");
        inspectCommandForSessionId(function() {
            assert.writeOK(siblingColl.insert({x: 1}));
        }, {shouldIncludeId: true, expectedId: implicitId});

        // A new database from the same connection should inherit the implicit session.
        const newCollSameConn = conn.getDB("testTwo").getCollection("foo");
        inspectCommandForSessionId(function() {
            assert.writeOK(newCollSameConn.insert({x: 1}));
        }, {shouldIncludeId: true, expectedId: implicitId});

        // A new database from a new connection should use a different implicit session.
        const newCollNewConn = new Monger(conn.host).getDB("test").getCollection("foo");
        inspectCommandForSessionId(function() {
            assert.writeOK(newCollNewConn.insert({x: 1}));
        }, {shouldIncludeId: true, differentFromId: implicitId});

        // The original implicit session should still live on the first database.
        inspectCommandForSessionId(function() {
            assert.writeOK(coll.insert({x: 1}));
        }, {shouldIncludeId: true, expectedId: implicitId});

        // Databases created from an explicit session should override any implicit sessions.
        const session = conn.startSession();
        const sessionColl = session.getDatabase("test").getCollection("foo");
        const explicitId = inspectCommandForSessionId(function() {
            assert.writeOK(sessionColl.insert({x: 1}));
        }, {shouldIncludeId: true, differentFromId: implicitId});

        assert(bsonBinaryEqual(session.getSessionId(), explicitId),
               "Expected the id of the explicit session to match the one sent, sent: " +
                   tojson(explicitId) + " explicit session id: " + tojson(session.getSessionId()));
        assert(bsonBinaryEqual(sessionColl.getDB().getSession().getSessionId(), explicitId),
               "Expected id of the database's session to match the explicit session's id, sent: " +
                   tojson(sessionColl.getDB().getSession().getSessionId()) +
                   ", explicit session id: " + tojson(session.getSessionId()));

        // The original implicit session should still live on the first database.
        inspectCommandForSessionId(function() {
            assert.writeOK(coll.insert({x: 1}));
        }, {shouldIncludeId: true, expectedId: implicitId});

        // New databases on the same connection as the explicit session should still inherit the
        // original implicit session.
        const newCollSameConnAfter = conn.getDB("testThree").getCollection("foo");
        inspectCommandForSessionId(function() {
            assert.writeOK(newCollSameConnAfter.insert({x: 1}));
        }, {shouldIncludeId: true, expectedId: implicitId});

        session.endSession();
        MongerRunner.stopMongerd(conn);
    }

    // Tests behavior when the test flag to disable implicit sessions is changed.
    function runTestTransitionToDisabled() {
        const conn = MongerRunner.runMongerd();

        // Existing implicit sessions should be erased when the disable flag is set.
        const coll = conn.getDB("test").getCollection("foo");
        const implicitId = inspectCommandForSessionId(function() {
            assert.writeOK(coll.insert({x: 1}));
        }, {shouldIncludeId: true});

        TestData.disableImplicitSessions = true;

        inspectCommandForSessionId(function() {
            assert.writeOK(coll.insert({x: 1}));
        }, {shouldIncludeId: false});

        // After the flag is unset, databases using existing connections with implicit sessions will
        // use the original implicit sessions again and new connections will create and use new
        // implicit sessions.
        TestData.disableImplicitSessions = false;

        inspectCommandForSessionId(function() {
            assert.writeOK(coll.insert({x: 1}));
        }, {shouldIncludeId: true, expectedId: implicitId});

        const newColl = conn.getDB("test").getCollection("foo");
        inspectCommandForSessionId(function() {
            assert.writeOK(newColl.insert({x: 1}));
        }, {shouldIncludeId: true, expectedId: implicitId});

        const newCollNewConn = new Monger(conn.host).getDB("test").getCollection("foo");
        inspectCommandForSessionId(function() {
            assert.writeOK(newCollNewConn.insert({x: 1}));
        }, {shouldIncludeId: true, differentFromId: implicitId});

        // Explicit sessions should not be affected by the disable flag being set.
        const session = conn.startSession();
        const sessionColl = session.getDatabase("test").getCollection("foo");
        const explicitId = inspectCommandForSessionId(function() {
            assert.writeOK(sessionColl.insert({x: 1}));
        }, {shouldIncludeId: true});

        TestData.disableImplicitSessions = true;

        inspectCommandForSessionId(function() {
            assert.writeOK(sessionColl.insert({x: 1}));
        }, {shouldIncludeId: true, expectedId: explicitId});

        session.endSession();
        MongerRunner.stopMongerd(conn);
    }

    // Tests behavior of implicit sessions when they are disabled via a test flag.
    function runTestDisabled() {
        const conn = MongerRunner.runMongerd();

        // Commands run without an explicit session should not use an implicit one.
        const coll = conn.getDB("test").getCollection("foo");
        inspectCommandForSessionId(function() {
            assert.writeOK(coll.insert({x: 1}));
        }, {shouldIncludeId: false});

        // Explicit sessions should still include session ids.
        const session = conn.startSession();
        const sessionColl = session.getDatabase("test").getCollection("foo");
        inspectCommandForSessionId(function() {
            assert.writeOK(sessionColl.insert({x: 1}));
        }, {shouldIncludeId: true});

        // Commands run in a parallel shell inherit the disable flag.
        TestData.inspectCommandForSessionId = inspectCommandForSessionId;
        const awaitShell = startParallelShell(function() {
            const parallelColl = db.getCollection("foo");
            TestData.inspectCommandForSessionId(function() {
                assert.writeOK(parallelColl.insert({x: 1}));
            }, {shouldIncludeId: false});
        }, conn.port);
        awaitShell();

        session.endSession();
        MongerRunner.stopMongerd(conn);
    }

    runTest();

    runTestTransitionToDisabled();

    assert(_shouldUseImplicitSessions());

    TestData.disableImplicitSessions = true;
    runTestDisabled();
})();
