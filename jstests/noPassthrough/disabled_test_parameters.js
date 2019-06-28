// Test that test-only set parameters are disabled.

(function() {
    'use strict';

    function assertFails(opts) {
        assert.eq(null, MongerRunner.runMongerd(opts), "Mongerd startup up");
    }

    function assertStarts(opts) {
        const mongerd = MongerRunner.runMongerd(opts);
        assert(mongerd, "Mongerd startup up");
        MongerRunner.stopMongerd(mongerd);
    }

    setJsTestOption('enableTestCommands', false);

    // enableTestCommands not specified.
    assertFails({
        'setParameter': {
            AlwaysRecordTraffic: 'false',
        },
    });

    // enableTestCommands specified as truthy.
    ['1', 'true'].forEach(v => {
        assertStarts({
            'setParameter': {
                enableTestCommands: v,
                enableIndexBuildsCoordinatorForCreateIndexesCommand: 'false',
            },
        });
    });

    // enableTestCommands specified as falsy.
    ['0', 'false'].forEach(v => {
        assertFails({
            'setParameter': {
                enableTestCommands: v,
                AlwaysRecordTraffic: 'false',
            },
        });
    });
}());
