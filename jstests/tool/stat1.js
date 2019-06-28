// stat1.js
// test mongerstat with authentication SERVER-3875
baseName = "tool_stat1";

var m = MongerRunner.runMongerd({auth: "", bind_ip: "127.0.0.1"});
db = m.getDB("admin");

db.createUser({user: "eliot", pwd: "eliot", roles: jsTest.adminUserRoles});
assert(db.auth("eliot", "eliot"), "auth failed");

var exitCode = MongerRunner.runMongerTool("mongerstat", {
    host: "127.0.0.1:" + m.port,
    username: "eliot",
    password: "eliot",
    rowcount: "1",
    authenticationDatabase: "admin",
});
assert.eq(exitCode, 0, "mongerstat should exit successfully with eliot:eliot");

exitCode = MongerRunner.runMongerTool("mongerstat", {
    host: "127.0.0.1:" + m.port,
    username: "eliot",
    password: "wrong",
    rowcount: "1",
    authenticationDatabase: "admin",
});
assert.neq(exitCode, 0, "mongerstat should exit with -1 with eliot:wrong");
MongerRunner.stopMongerd(m);