// Reconfigure WiredTiger test cases
//
// Start our own instance of mongerd so that are settings tests
// do not cause issues for other tests
//
var ss = db.serverStatus();

// Test is only valid in the WT suites which run against a mongerd with WiredTiger enabled
if (ss.storageEngine.name !== "wiredTiger") {
    print("Skipping reconfigwt.js since this server does not have WiredTiger enabled");
} else {
    var conn = MongerRunner.runMongerd();

    var admin = conn.getDB("admin");

    function reconfigure(str) {
        ret = admin.runCommand({setParameter: 1, "wiredTigerEngineRuntimeConfig": str});
        print("ret: " + tojson(ret));
        return ret;
    }

    // See the WT_CONNECTION:reconfigure documentation for a list of valid options
    // http://source.wiredtiger.com/develop/struct_w_t___c_o_n_n_e_c_t_i_o_n.html#a579141678af06217b22869cbc604c6d4
    assert.commandWorked(reconfigure("eviction_target=81"));
    assert.eq(
        "eviction_target=81",
        admin.adminCommand({getParameter: 1,
                            "wiredTigerEngineRuntimeConfig": 1})["wiredTigerEngineRuntimeConfig"]);
    assert.commandWorked(reconfigure("cache_size=81M"));
    assert.eq(
        "cache_size=81M",
        admin.adminCommand({getParameter: 1,
                            "wiredTigerEngineRuntimeConfig": 1})["wiredTigerEngineRuntimeConfig"]);
    assert.commandWorked(reconfigure("eviction_dirty_target=82"));
    assert.commandWorked(reconfigure("shared_cache=(chunk=11MB, name=bar, reserve=12MB, size=1G)"));

    // Negative tests - bad input to mongerd
    assert.commandFailed(reconfigure("abc\0as"));

    // Negative tests - bad input to wt
    assert.commandFailed(reconfigure("eviction_target=a"));
    assert.commandFailed(reconfigure("fake_config_key=1"));
    assert.eq(
        "shared_cache=(chunk=11MB, name=bar, reserve=12MB, size=1G)",
        admin.adminCommand({getParameter: 1,
                            "wiredTigerEngineRuntimeConfig": 1})["wiredTigerEngineRuntimeConfig"]);

    MongerRunner.stopMongerd(conn);
}
