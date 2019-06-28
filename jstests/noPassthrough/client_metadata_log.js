/**
 * Test that verifies client metadata is logged into log file on new connections.
 * @tags: [requires_sharding]
 */
(function() {
    'use strict';

    let checkLog = function(conn) {
        let coll = conn.getCollection("test.foo");
        assert.writeOK(coll.insert({_id: 1}));

        print(`Checking ${conn.fullOptions.logFile} for client metadata message`);
        let log = cat(conn.fullOptions.logFile);

        assert(
            /received client metadata from .*: { application: { name: ".*" }, driver: { name: ".*", version: ".*" }, os: { type: ".*", name: ".*", architecture: ".*", version: ".*" } }/
                .test(log),
            "'received client metadata' log line missing in log file!\n" + "Log file contents: " +
                conn.fullOptions.logFile +
                "\n************************************************************\n" + log +
                "\n************************************************************");
    };

    // Test MongerD
    let testMongerD = function() {
        let conn = MongerRunner.runMongerd({useLogFiles: true});
        assert.neq(null, conn, 'mongerd was unable to start up');

        checkLog(conn);

        MongerRunner.stopMongerd(conn);
    };

    // Test MongerS
    let testMongerS = function() {
        let options = {
            mongersOptions: {useLogFiles: true},
        };

        let st = new ShardingTest({shards: 1, mongers: 1, other: options});

        checkLog(st.s0);

        // Validate db.currentOp() contains mongers information
        let curOp = st.s0.adminCommand({currentOp: 1});
        print(tojson(curOp));

        var inprogSample = null;
        for (let inprog of curOp.inprog) {
            if (inprog.hasOwnProperty("clientMetadata") &&
                inprog.clientMetadata.hasOwnProperty("mongers")) {
                inprogSample = inprog;
                break;
            }
        }

        assert.neq(inprogSample.clientMetadata.mongers.host, "unknown");
        assert.neq(inprogSample.clientMetadata.mongers.client, "unknown");
        assert.neq(inprogSample.clientMetadata.mongers.version, "unknown");

        st.stop();
    };

    testMongerD();
    testMongerS();
})();
