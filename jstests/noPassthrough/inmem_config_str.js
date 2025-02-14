// SERVER-28179 Test the startup of in-memory storage engine using --inMemoryEngineConfigString
(function() {
    'use strict';

    if (jsTest.options().storageEngine !== "inMemory") {
        jsTestLog("Skipping test because storageEngine is not inMemory");
        return;
    }

    var mongerd = MongerRunner.runMongerd({
        storageEngine: 'inMemory',
        inMemoryEngineConfigString: 'eviction=(threads_min=1)',
    });
    assert.neq(null, mongerd, "mongerd failed to started up with --inMemoryEngineConfigString");

    MongerRunner.stopMongerd(mongerd);
}());
