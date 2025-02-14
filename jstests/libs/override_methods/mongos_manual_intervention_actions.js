/**
 * If the config primary steps down during a metadata command, mongers will internally retry the
 * command. On the retry, the command may fail with the error "ManualInterventionRequired" if
 * the earlier try left the config database in an inconsistent state.
 *
 * This override allows for automating the manual cleanup by catching the
 * "ManualInterventionRequired" error, performing the cleanup, and transparently retrying the
 * command.
 */

var ManualInterventionActions = (function() {
    /**
     * Remove all the chunk documents from the given namespace. Deletes are performed one at a
     * time to bypass auto_retry_on_network_error.js multi remove check.
     */
    let removeChunks = function(mongersConn, ns) {
        let stillHasChunks = true;

        while (stillHasChunks) {
            let writeRes = assert.writeOK(mongersConn.getDB('config').chunks.remove(
                {ns: ns}, {justOne: true, writeConcern: {w: 'majority'}}));
            stillHasChunks = writeRes.nRemoved > 0;
        }
    };

    this.removePartiallyWrittenChunks = function(mongersConn, ns, cmdObj, numAttempts) {
        print("command " + tojson(cmdObj) + " failed after " + numAttempts +
              " attempts due to seeing partially written chunks for collection " + ns +
              ", probably due to a previous failed shardCollection attempt. Manually" +
              " deleting chunks for " + ns + " from config.chunks and retrying the command.");

        removeChunks(mongersConn, ns);
    };

    this.removePartiallyWrittenChunksAndDropCollection = function(
        mongersConn, ns, cmdObj, numAttempts) {
        print("command " + tojson(cmdObj) + " failed after " + numAttempts +
              " attempts due to seeing partially written chunks for collection " + ns +
              ", probably due to a previous failed shardCollection attempt. Manually" +
              " deleting chunks for " + ns + " from config.chunks" +
              ", dropping the collection, and retrying the command.");

        removeChunks(mongersConn, ns);
        const[dbName, collName] = ns.split(".");
        assert.commandWorked(
            mongersConn.getDB(dbName).runCommand({"drop": collName, writeConcern: {w: "majority"}}));
    };

    return this;
})();

(function() {

    const mongerRunCommandOriginal = Monger.prototype.runCommand;

    Monger.prototype.runCommand = function runCommand(dbName, cmdObj, options) {
        const cmdName = Object.keys(cmdObj)[0];
        const commandsToRetry =
            new Set(["mapReduce", "mapreduce", "shardCollection", "shardcollection"]);

        if (!commandsToRetry.has(cmdName)) {
            return mongerRunCommandOriginal.apply(this, arguments);
        }

        const maxAttempts = 10;
        let numAttempts = 0;
        let res;

        while (numAttempts < maxAttempts) {
            res = mongerRunCommandOriginal.apply(this, arguments);
            ++numAttempts;

            if (res.ok === 1 || res.code !== ErrorCodes.ManualInterventionRequired ||
                numAttempts === maxAttempts) {
                break;
            }

            print("Manual intervention retry attempt# " + numAttempts + " because of error: " +
                  tojson(res));

            if (cmdName === "shardCollection" || cmdName === "shardcollection") {
                const ns = cmdObj[cmdName];
                ManualInterventionActions.removePartiallyWrittenChunks(
                    this, ns, cmdObj, numAttempts);
            } else if (cmdName === "mapReduce" || cmdName === "mapreduce") {
                const out = cmdObj.out;

                // The output collection can be specified as a string argument to the mapReduce
                // command's 'out' option, or nested under 'out.replace', 'out.merge', or
                // 'out.reduce'.
                let outCollName;
                if (typeof out === "string") {
                    outCollName = out;
                } else if (typeof out === "object") {
                    outCollName = out.replace || out.merge || out.reduce;
                } else {
                    print("Could not parse the output collection's name from 'out' option in " +
                          tojson(cmdObj) + "; not retrying on ManualInterventionRequired error " +
                          tojson(res));
                    break;
                }

                // The output collection's database can optionally be specified under 'out.db',
                // else it defaults to the input collection's database.
                const outDbName = out.db || dbName;

                const ns = outDbName + "." + outCollName;
                ManualInterventionActions.removePartiallyWrittenChunksAndDropCollection(
                    this, ns, cmdObj, numAttempts);
            }
        }
        return res;
    };
})();
