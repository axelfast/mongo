/**
 * Tests that the shell correctly checks for program extensions in Windows environments.
 */

(function() {
    'use strict';

    if (_isWindows()) {
        const filename = 'jstests/noPassthrough/libs/testWindowsExtension.bat';

        clearRawMongerProgramOutput();
        const result = runMongerProgram(filename);
        assert.eq(result, 42);
    } else {
        jsTestLog("This test is only relevant for Windows environments.");
    }
})();
