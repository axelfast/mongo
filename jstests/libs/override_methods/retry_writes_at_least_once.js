/**
 * Overrides Monger.prototype.runCommand and Monger.prototype.runCommandWithMetadata to retry all
 * retryable writes at least once, randomly more than that, regardless of the outcome of the
 * command. Returns the result of the latest attempt.
 */
(function() {
    "use strict";

    load("jstests/libs/override_methods/override_helpers.js");
    load("jstests/libs/retryable_writes_util.js");

    Random.setRandomSeed();

    const kExtraRetryProbability = 0.2;

    const mongerRunCommandOriginal = Monger.prototype.runCommand;
    const mongerRunCommandWithMetadataOriginal = Monger.prototype.runCommandWithMetadata;

    Monger.prototype.runCommand = function runCommand(dbName, cmdObj, options) {
        return runWithRetries(this, cmdObj, mongerRunCommandOriginal, arguments);
    };

    Monger.prototype.runCommandWithMetadata = function runCommandWithMetadata(
        dbName, metadata, cmdObj) {
        return runWithRetries(this, cmdObj, mongerRunCommandWithMetadataOriginal, arguments);
    };

    function runWithRetries(monger, cmdObj, clientFunction, clientFunctionArguments) {
        let cmdName = Object.keys(cmdObj)[0];

        // If the command is in a wrapped form, then we look for the actual command object
        // inside the query/$query object.
        if (cmdName === "query" || cmdName === "$query") {
            cmdObj = cmdObj[cmdName];
            cmdName = Object.keys(cmdObj)[0];
        }

        const isRetryableWriteCmd = RetryableWritesUtil.isRetryableWriteCmdName(cmdName);
        const canRetryWrites = _ServerSession.canRetryWrites(cmdObj);

        let res = clientFunction.apply(monger, clientFunctionArguments);

        if (isRetryableWriteCmd && canRetryWrites) {
            let retryAttempt = 1;
            do {
                print("*** Retry attempt: " + retryAttempt + ", for command: " + cmdName +
                      " with txnNumber: " + tojson(cmdObj.txnNumber) + ", and lsid: " +
                      tojson(cmdObj.lsid));
                ++retryAttempt;
                res = clientFunction.apply(monger, clientFunctionArguments);
            } while (Random.rand() <= kExtraRetryProbability);
        }

        return res;
    }

    OverrideHelpers.prependOverrideInParallelShell(
        "jstests/libs/override_methods/retry_writes_at_least_once.js");
})();
