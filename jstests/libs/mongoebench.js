"use strict";

var {runMongereBench} = (function() {

    /**
     * Spawns a mongerebench process with the specified options.
     *
     * If a plain JavaScript object is specified as the 'config' parameter, then it is serialized to
     * a file as a JSON string which is then specified as the config file for the mongerebench
     * process.
     */
    function runMongereBench(config, options = {}) {
        const args = ["mongerebench"];

        if (typeof config === "object") {
            const filename = MongerRunner.dataPath + "mongerebench_config.json";
            writeFile(filename, tojson(config));
            args.push(filename);
        } else if (typeof config === "string") {
            args.push(config);
        } else {
            throw new Error("'config' parameter must be a string or an object");
        }

        if (!options.hasOwnProperty("dbpath")) {
            options.dbpath = MongerRunner.dataDir;
        }

        for (let key of Object.keys(options)) {
            const value = options[key];
            if (value === null || value === undefined) {
                throw new Error(
                    "Value '" + value + "' for '" + key +
                    "' option is ambiguous; specify {flag: ''} to add --flag command line" +
                    " options'");
            }

            args.push("--" + key);
            if (value !== "") {
                args.push(value.toString());
            }
        }

        const exitCode = _runMongerProgram(...args);
        assert.eq(0, exitCode, "encountered an error in mongerebench");
    }

    return {runMongereBench};
})();
