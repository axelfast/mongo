// Test that isInteractive() returns false when running script or --eval
// and true when running in interactive mode

(function() {
    "use strict";

    if (!_isWindows()) {
        clearRawMongerProgramOutput();
        var rc = runProgram("./monger", "--nodb", "--quiet", "--eval", "print(isInteractive())");
        assert.eq(rc, 0);
        var output = rawMongerProgramOutput();
        var response = (output.split('\n').slice(-2)[0]).split(' ')[1];
        assert.eq(response, "false", "Expected 'false' in script mode");
        // now try interactive
        clearRawMongerProgramOutput();
        rc = runProgram(
            "./monger", "--nodb", "--quiet", "--shell", "--eval", "print(isInteractive()); quit()");
        assert.eq(rc, 0);
        output = rawMongerProgramOutput();
        response = (output.split('\n').slice(-2)[0]).split(' ')[1];
        assert.eq(response, "true", "Expected 'true' in interactive mode");
    }

})();
