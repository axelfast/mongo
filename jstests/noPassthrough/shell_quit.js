(function() {
    'use strict';
    var checkShell = function(retCode) {
        var args = [
            "monger",
            "--nodb",
            "--eval",
            "quit(" + retCode + ");",
        ];

        var actualRetCode = _runMongerProgram.apply(null, args);
        assert.eq(retCode, actualRetCode);
    };

    checkShell(0);
    checkShell(5);
})();
