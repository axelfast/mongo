// monger tool tests, very basic to start with

baseName = "jstests_tool_tool1";
dbPath = MongerRunner.dataPath + baseName + "/";
externalPath = MongerRunner.dataPath + baseName + "_external/";
externalBaseName = "export.json";
externalFile = externalPath + externalBaseName;

function fileSize() {
    var l = listFiles(externalPath);
    for (var i = 0; i < l.length; i++) {
        if (l[i].baseName == externalBaseName)
            return l[i].size;
    }
    return -1;
}

resetDbpath(externalPath);

var m = MongerRunner.runMongerd({dbpath: dbPath, bind_ip: "127.0.0.1"});
c = m.getDB(baseName).getCollection(baseName);
c.save({a: 1});
assert(c.findOne());

var exitCode = MongerRunner.runMongerTool("mongerdump", {
    host: "127.0.0.1:" + m.port,
    out: externalPath,
});
assert.eq(0, exitCode, "mongerdump failed to dump data from mongerd");

c.drop();

exitCode = MongerRunner.runMongerTool("mongerrestore", {
    host: "127.0.0.1:" + m.port,
    dir: externalPath,
});
assert.eq(0, exitCode, "mongerrestore failed to restore data to mongerd");

assert.soon("c.findOne()", "mongerdump then restore has no data w/sleep");
assert(c.findOne(), "mongerdump then restore has no data");
assert.eq(1, c.findOne().a, "mongerdump then restore has no broken data");

resetDbpath(externalPath);

assert.eq(-1, fileSize(), "mongerexport prep invalid");

exitCode = MongerRunner.runMongerTool("mongerexport", {
    host: "127.0.0.1:" + m.port,
    db: baseName,
    collection: baseName,
    out: externalFile,
});
assert.eq(
    0, exitCode, "mongerexport failed to export collection '" + c.getFullName() + "' from mongerd");

assert.lt(10, fileSize(), "file size changed");

c.drop();

exitCode = MongerRunner.runMongerTool("mongerimport", {
    host: "127.0.0.1:" + m.port,
    db: baseName,
    collection: baseName,
    file: externalFile,
});
assert.eq(
    0, exitCode, "mongerimport failed to import collection '" + c.getFullName() + "' into mongerd");

assert.soon("c.findOne()", "monger import json A");
assert(c.findOne() && 1 == c.findOne().a, "monger import json B");
MongerRunner.stopMongerd(m);