// Tests --networkMessageCompressors options.

(function() {
    'use strict';

    var runTest = function(optionValue, expected) {
        jsTest.log("Testing with --networkMessageCompressors=\"" + optionValue + "\" expecting: " +
                   expected);
        var monger = MongoRunner.runMongod({networkMessageCompressors: optionValue});
        assert.commandWorked(monger.adminCommand({isMaster: 1}));
        clearRawMongoProgramOutput();
        assert.eq(runMongoProgram("monger",
                                  "--eval",
                                  "tostrictjson(db.isMaster());",
                                  "--port",
                                  monger.port,
                                  "--networkMessageCompressors=snappy"),
                  0);

        var output = rawMongoProgramOutput()
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
        MongoRunner.stopMongod(monger);
    };

    assert.isnull(MongoRunner.runMongod({networkMessageCompressors: "snappy,disabled"}));

    runTest("snappy", ["snappy"]);
    runTest("disabled", undefined);

}());
