// Tests --networkMessageCompressors options.

(function() {
    'use strict';

    var runTest = function(optionValue, expected) {
        jsTest.log("Testing with --networkMessageCompressors=\"" + optionValue + "\" expecting: " +
                   expected);
        var monger = MongerRunner.runMongerd({networkMessageCompressors: optionValue});
        assert.commandWorked(monger.adminCommand({isMaster: 1}));
        clearRawMongerProgramOutput();
        assert.eq(runMongerProgram("monger",
                                  "--eval",
                                  "tostrictjson(db.isMaster());",
                                  "--port",
                                  monger.port,
                                  "--networkMessageCompressors=snappy"),
                  0);

        var output = rawMongerProgramOutput()
                         .split("\n")
                         .map(function(str) {
                             str = str.replace(/^sh[0-9]+\| /, "");
                             if (!/^{/.test(str)) {
                                 return "";
                             }
                             return str;
                         })
                         .join("\n")
                         .trim();

        output = JSON.parse(output);

        assert.eq(output.compression, expected);
        MongerRunner.stopMongerd(monger);
    };

    assert.isnull(MongerRunner.runMongerd({networkMessageCompressors: "snappy,disabled"}));

    runTest("snappy", ["snappy"]);
    runTest("disabled", undefined);

}());
