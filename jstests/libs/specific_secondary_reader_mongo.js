"use strict";

// This is a subclass of Monger, which stores both the default connection
// and the direct connection to a specific secondary node. For reads with
// readPreference "secondary", they are sent to the specific secondary node.

function SpecificSecondaryReaderMonger(host, secondary) {
    var defaultMonger = new Monger(host);
    var secondaryMonger = new Monger(secondary);

    // This overrides the default runCommand() in Monger
    this.runCommand = function runCommand(dbName, commandObj, options) {
        // If commandObj is specified with the readPreference "secondary", then use direct
        // connection to secondary. Otherwise use the default connection.
        if (commandObj.hasOwnProperty("$readPreference")) {
            if (commandObj.$readPreference.mode === "secondary") {
                return secondaryMonger.runCommand(dbName, commandObj, options);
            }
        }
        return defaultMonger.runCommand(dbName, commandObj, options);
    };

    return new Proxy(this, {
        get: function get(target, property, receiver) {
            // If the property is defined on the SpecificSecondaryReaderMonger instance itself, then
            // return it.  Otherwise, get the value of the property from the Monger instance.
            if (target.hasOwnProperty(property)) {
                return target[property];
            }
            var value = defaultMonger[property];
            if (typeof value === "function") {
                if (property === "getDB" || property === "startSession") {
                    // 'receiver' is the Proxy object.
                    return value.bind(receiver);
                }
                return value.bind(defaultMonger);
            }
            return value;
        },

        set: function set(target, property, value, receiver) {
            // Delegate setting the value of any property to the Monger instance so
            // that it can be accessed in functions acting on the Monger instance
            // directly instead of this Proxy.  For example, the "slaveOk" property
            // needs to be set on the Monger instance in order for the query options
            // bit to be set correctly.
            defaultMonger[property] = value;
            return true;
        },
    });
}
