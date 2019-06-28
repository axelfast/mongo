/**
 * Tests that various forms of normal and abnormal shutdown write to the log files as expected.
 * @tags: [requires_sharding]
 */

(function() {

    function makeShutdownByCrashFn(crashHow) {
        return function(conn) {
            var admin = conn.getDB("admin");
            assert.commandWorked(admin.runCommand(
                {configureFailPoint: "crashOnShutdown", mode: "alwaysOn", data: {how: crashHow}}));
            admin.shutdownServer();
        };
    }

    function makeRegExMatchFn(pattern) {
        return function(text) {
            return pattern.test(text);
        };
    }

    function testShutdownLogging(launcher, crashFn, matchFn, expectedExitCode) {
        clearRawMongerProgramOutput();
        var conn = launcher.start({});

        function checkOutput() {
            var logContents = rawMongerProgramOutput();
            function printLog() {
                // We can't just return a string because it will be well over the max
                // line length.
                // So we just print manually.
                print("================ BEGIN LOG CONTENTS ==================");
                logContents.split(/\n/).forEach((line) => {
                    print(line);
                });
                print("================ END LOG CONTENTS =====================");
                return "";
            }

            assert(matchFn(logContents), printLog);
        }

        crashFn(conn);
        launcher.stop(conn, undefined, {allowedExitCode: expectedExitCode});
        checkOutput();
    }

    function runAllTests(launcher) {
        const SIGSEGV = 11;
        const SIGABRT = 6;
        testShutdownLogging(launcher, function(conn) {
            conn.getDB('admin').shutdownServer();
        }, makeRegExMatchFn(/shutdown command received/), MongerRunner.EXIT_CLEAN);

        testShutdownLogging(launcher,
                            makeShutdownByCrashFn('fault'),
                            makeRegExMatchFn(/Invalid access at address[\s\S]*printStackTrace/),
                            -SIGSEGV);

        testShutdownLogging(launcher,
                            makeShutdownByCrashFn('abort'),
                            makeRegExMatchFn(/Got signal[\s\S]*printStackTrace/),
                            -SIGABRT);
    }

    if (_isWindows()) {
        print("SKIPPING TEST ON WINDOWS");
        return;
    }

    if (_isAddressSanitizerActive()) {
        print("SKIPPING TEST ON ADDRESS SANITIZER BUILD");
        return;
    }

    (function testMongerd() {
        print("********************\nTesting exit logging in mongerd\n********************");

        runAllTests({
            start: function(opts) {
                var actualOpts = {nojournal: ""};
                Object.extend(actualOpts, opts);
                return MongerRunner.runMongerd(actualOpts);
            },

            stop: MongerRunner.stopMongerd
        });
    }());

    (function testMongers() {
        print("********************\nTesting exit logging in mongers\n********************");

        var st = new ShardingTest({shards: 1});
        var mongersLauncher = {
            start: function(opts) {
                var actualOpts = {configdb: st._configDB};
                Object.extend(actualOpts, opts);
                return MongerRunner.runMongers(actualOpts);
            },

            stop: MongerRunner.stopMongers
        };

        runAllTests(mongersLauncher);
        st.stop();
    }());

}());
