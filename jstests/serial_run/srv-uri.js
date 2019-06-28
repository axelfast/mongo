(function() {
    "use strict";
    const md = MongerRunner.runMongerd({port: "27017", dbpath: MongerRunner.dataPath});
    assert.neq(null, md, "unable to start mongerd");
    const targetURI = 'mongerdb+srv://test1.test.build.10gen.cc./?ssl=false';
    const exitCode = runMongerProgram('monger', targetURI, '--eval', ';');
    assert.eq(exitCode, 0, "Failed to connect with a `mongerdb+srv://` style URI.");
    MongerRunner.stopMongerd(md);
})();
