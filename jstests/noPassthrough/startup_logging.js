/**
 * Tests that normal startup writes to the log files as expected.
 */

(function() {

    'use strict';

    function makeRegExMatchFn(pattern) {
        return function(text) {
            return pattern.test(text);
        };
    }

    function testStartupLogging(launcher, matchFn, expectedExitCode) {
        assert(matchFn(rawMongerProgramOutput()));
    }

    function validateWaitingMessage(launcher) {
        clearRawMongerProgramOutput();
        var conn = launcher.start({});
        launcher.stop(conn, undefined, {});
        testStartupLogging(launcher, makeRegExMatchFn(/waiting for connections on port/));
    }

    print("********************\nTesting startup logging in mongerd\n********************");

    validateWaitingMessage({
        start: function(opts) {
            return MongerRunner.runMongerd(opts);
        },
        stop: MongerRunner.stopMongerd
    });

}());
