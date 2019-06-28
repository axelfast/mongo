// monger.js

// Defined in monger.cpp

if (!Monger.prototype) {
    throw Error("Monger.prototype not defined");
}

if (!Monger.prototype.find)
    Monger.prototype.find = function(ns, query, fields, limit, skip, batchSize, options) {
        throw Error("find not implemented");
    };
if (!Monger.prototype.insert)
    Monger.prototype.insert = function(ns, obj) {
        throw Error("insert not implemented");
    };
if (!Monger.prototype.remove)
    Monger.prototype.remove = function(ns, pattern) {
        throw Error("remove not implemented");
    };
if (!Monger.prototype.update)
    Monger.prototype.update = function(ns, query, obj, upsert) {
        throw Error("update not implemented");
    };

if (typeof mongerInject == "function") {
    mongerInject(Monger.prototype);
}

Monger.prototype.setSlaveOk = function(value) {
    if (value == undefined)
        value = true;
    this.slaveOk = value;
};

Monger.prototype.getSlaveOk = function() {
    return this.slaveOk || false;
};

Monger.prototype.getDB = function(name) {
    if ((jsTest.options().keyFile) &&
        ((typeof this.authenticated == 'undefined') || !this.authenticated)) {
        jsTest.authenticate(this);
    }
    // There is a weird issue where typeof(db._name) !== "string" when the db name
    // is created from objects returned from native C++ methods.
    // This hack ensures that the db._name is always a string.
    if (typeof(name) === "object") {
        name = name.toString();
    }
    return new DB(this, name);
};

Monger.prototype._getDatabaseNamesFromPrivileges = function() {
    'use strict';

    const ret = this.adminCommand({connectionStatus: 1, showPrivileges: 1});
    if (!ret.ok) {
        throw _getErrorWithCode(ret, "Failed to acquire database information from privileges");
    }

    const privileges = (ret.authInfo || {}).authenticatedUserPrivileges;
    if (privileges === undefined) {
        return [];
    }

    return privileges
        .filter(function(priv) {
            // Find all named databases in priv list.
            return ((priv.resource || {}).db || '').length > 0;
        })
        .map(function(priv) {
            // Return just the names.
            return priv.resource.db;
        })
        .filter(function(db, idx, arr) {
            // Make sure the list is unique
            return arr.indexOf(db) === idx;
        })
        .sort();
};

Monger.prototype.getDBs = function(driverSession = this._getDefaultSession(),
                                  filter = undefined,
                                  nameOnly = undefined,
                                  authorizedDatabases = undefined) {

    return function(driverSession, filter, nameOnly, authorizedDatabases) {
        'use strict';

        let cmdObj = {listDatabases: 1};
        if (filter !== undefined) {
            cmdObj.filter = filter;
        }
        if (nameOnly !== undefined) {
            cmdObj.nameOnly = nameOnly;
        }
        if (authorizedDatabases !== undefined) {
            cmdObj.authorizedDatabases = authorizedDatabases;
        }

        if (driverSession._isExplicit || !jsTest.options().disableImplicitSessions) {
            cmdObj = driverSession._serverSession.injectSessionId(cmdObj);
        }

        const res = this.adminCommand(cmdObj);
        if (!res.ok) {
            // If "Unauthorized" was returned by the back end and we haven't explicitly
            // asked for anything difficult to provide from userspace, then we can
            // fallback on inspecting the user's permissions.
            // This means that:
            //   * filter must be undefined, as reimplementing that logic is out of scope.
            //   * nameOnly must not be false as we can't infer size information.
            //   * authorizedDatabases must not be false as those are the only DBs we can infer.
            // Note that if the above are valid and we get Unauthorized, that also means
            // that we MUST be talking to a pre-4.0 mongerd.
            //
            // Like the server response mode, this path will return a simple list of
            // names if nameOnly is specified as true.
            // If nameOnly is undefined, we come as close as we can to what the
            // server would return by supplying the databases key of the returned
            // object.  Other information is unavailable.
            if ((res.code === ErrorCodes.Unauthorized) && (filter === undefined) &&
                (nameOnly !== false) && (authorizedDatabases !== false)) {
                const names = this._getDatabaseNamesFromPrivileges();
                if (nameOnly === true) {
                    return names;
                } else {
                    return {
                        databases: names.map(function(x) {
                            return {name: x};
                        }),
                    };
                }
            }
            throw _getErrorWithCode(res, "listDatabases failed:" + tojson(res));
        }

        if (nameOnly) {
            return res.databases.map(function(db) {
                return db.name;
            });
        }

        return res;
    }.call(this, driverSession, filter, nameOnly, authorizedDatabases);
};

Monger.prototype.adminCommand = function(cmd) {
    return this.getDB("admin").runCommand(cmd);
};

/**
 * Returns all log components and current verbosity values
 */
Monger.prototype.getLogComponents = function(driverSession = this._getDefaultSession()) {
    var cmdObj = {getParameter: 1, logComponentVerbosity: 1};
    if (driverSession._isExplicit || !jsTest.options().disableImplicitSessions) {
        cmdObj = driverSession._serverSession.injectSessionId(cmdObj);
    }

    var res = this.adminCommand(cmdObj);
    if (!res.ok)
        throw _getErrorWithCode(res, "getLogComponents failed:" + tojson(res));
    return res.logComponentVerbosity;
};

/**
 * Accepts optional second argument "component",
 * string of form "storage.journaling"
 */
Monger.prototype.setLogLevel = function(
    logLevel, component, driverSession = this._getDefaultSession()) {
    componentNames = [];
    if (typeof component === "string") {
        componentNames = component.split(".");
    } else if (component !== undefined) {
        throw Error("setLogLevel component must be a string:" + tojson(component));
    }
    var vDoc = {verbosity: logLevel};

    // nest vDoc
    for (var key, obj; componentNames.length > 0;) {
        obj = {};
        key = componentNames.pop();
        obj[key] = vDoc;
        vDoc = obj;
    }

    var cmdObj = {setParameter: 1, logComponentVerbosity: vDoc};
    if (driverSession._isExplicit || !jsTest.options().disableImplicitSessions) {
        cmdObj = driverSession._serverSession.injectSessionId(cmdObj);
    }

    var res = this.adminCommand(cmdObj);
    if (!res.ok)
        throw _getErrorWithCode(res, "setLogLevel failed:" + tojson(res));
    return res;
};

Monger.prototype.getDBNames = function() {
    return this.getDBs().databases.map(function(z) {
        return z.name;
    });
};

Monger.prototype.getCollection = function(ns) {
    var idx = ns.indexOf(".");
    if (idx < 0)
        throw Error("need . in ns");
    var db = ns.substring(0, idx);
    var c = ns.substring(idx + 1);
    return this.getDB(db).getCollection(c);
};

Monger.prototype.toString = function() {
    return "connection to " + this.host;
};
Monger.prototype.tojson = Monger.prototype.toString;

/**
 * Sets the read preference.
 *
 * @param mode {string} read preference mode to use. Pass null to disable read
 *     preference.
 * @param tagSet {Array.<Object>} optional. The list of tags to use, order matters.
 *     Note that this object only keeps a shallow copy of this array.
 */
Monger.prototype.setReadPref = function(mode, tagSet) {
    if ((this._readPrefMode === "primary") && (typeof(tagSet) !== "undefined") &&
        (Object.keys(tagSet).length > 0)) {
        // we allow empty arrays/objects or no tagSet for compatibility reasons
        throw Error("Can not supply tagSet with readPref mode primary");
    }
    this._setReadPrefUnsafe(mode, tagSet);
};

// Set readPref without validating. Exposed so we can test the server's readPref validation.
Monger.prototype._setReadPrefUnsafe = function(mode, tagSet) {
    this._readPrefMode = mode;
    this._readPrefTagSet = tagSet;
};

Monger.prototype.getReadPrefMode = function() {
    return this._readPrefMode;
};

Monger.prototype.getReadPrefTagSet = function() {
    return this._readPrefTagSet;
};

// Returns a readPreference object of the type expected by mongers.
Monger.prototype.getReadPref = function() {
    var obj = {}, mode, tagSet;
    if (typeof(mode = this.getReadPrefMode()) === "string") {
        obj.mode = mode;
    } else {
        return null;
    }
    // Server Selection Spec: - if readPref mode is "primary" then the tags field MUST
    // be absent. Ensured by setReadPref.
    if (Array.isArray(tagSet = this.getReadPrefTagSet())) {
        obj.tags = tagSet;
    }

    return obj;
};

/**
 * Sets the read concern.
 *
 * @param level {string} read concern level to use. Pass null to disable read concern.
 */
Monger.prototype.setReadConcern = function(level) {
    if (!level) {
        this._readConcernLevel = undefined;
    } else if (level === "local" || level === "majority") {
        this._readConcernLevel = level;
    } else {
        throw Error("Invalid read concern.");
    }
};

/**
 * Gets the read concern.
 */
Monger.prototype.getReadConcern = function() {
    return this._readConcernLevel;
};

connect = function(url, user, pass) {
    if (url instanceof MongerURI) {
        user = url.user;
        pass = url.password;
        url = url.uri;
    }
    if (user && !pass)
        throw Error("you specified a user and not a password.  " +
                    "either you need a password, or you're using the old connect api");

    // Validate connection string "url" as "hostName:portNumber/databaseName"
    //                                  or "hostName/databaseName"
    //                                  or "databaseName"
    //                                  or full monger uri.
    var urlType = typeof url;
    if (urlType == "undefined") {
        throw Error("Missing connection string");
    }
    if (urlType != "string") {
        throw Error("Incorrect type \"" + urlType + "\" for connection string \"" + tojson(url) +
                    "\"");
    }
    url = url.trim();
    if (0 == url.length) {
        throw Error("Empty connection string");
    }

    if (!url.startsWith("mongerdb://") && !url.startsWith("mongerdb+srv://")) {
        const colon = url.lastIndexOf(":");
        const slash = url.lastIndexOf("/");
        if (url.split("/").length > 1) {
            url = url.substring(0, slash).replace(/\//g, "%2F") + url.substring(slash);
        }
        if (slash == 0) {
            throw Error("Failed to parse mongerdb:// URL: " + url);
        }
        if (slash == -1 && colon == -1) {
            url = "mongerdb://127.0.0.1:27017/" + url;
        } else if (slash != -1) {
            url = "mongerdb://" + url;
        }
    }

    var atPos = url.indexOf("@");
    var protocolPos = url.indexOf("://");
    var safeURL = url;
    if (atPos != -1 && protocolPos != -1) {
        safeURL = url.substring(0, protocolPos + 3) + url.substring(atPos + 1);
    }
    chatty("connecting to: " + safeURL);
    try {
        var m = new Monger(url);
    } catch (e) {
        if (url.indexOf(".mongerdb.net") != -1) {
            print("\n\n*** It looks like this is a MongerDB Atlas cluster. Please ensure that your" +
                  " IP whitelist allows connections from your network.\n\n");
        }

        throw e;
    }
    var db = m.getDB(m.defaultDB);

    if (user && pass) {
        if (!db.auth(user, pass)) {
            throw Error("couldn't login");
        }
    }

    if (_shouldUseImplicitSessions()) {
        chatty("Implicit session: " + db.getSession());
    }

    // Implicit sessions should not be used when opening a connection. In particular, the buildInfo
    // command is erroneously marked as requiring auth in MongerDB 3.6 and therefore fails if a
    // logical session id is included in the request.
    const originalTestData = TestData;
    TestData = Object.merge(originalTestData, {disableImplicitSessions: true});
    try {
        // Check server version
        var serverVersion = db.version();
        chatty("MongerDB server version: " + serverVersion);

        var shellVersion = version();
        if (serverVersion.slice(0, 3) != shellVersion.slice(0, 3)) {
            chatty("WARNING: shell and server versions do not match");
        }
    } finally {
        TestData = originalTestData;
    }

    return db;
};

/** deprecated, use writeMode below
 *
 */
Monger.prototype.useWriteCommands = function() {
    return (this.writeMode() != "legacy");
};

Monger.prototype.forceWriteMode = function(mode) {
    this._writeMode = mode;
};

Monger.prototype.hasWriteCommands = function() {
    var hasWriteCommands = (this.getMinWireVersion() <= 2 && 2 <= this.getMaxWireVersion());
    return hasWriteCommands;
};

Monger.prototype.hasExplainCommand = function() {
    var hasExplain = (this.getMinWireVersion() <= 3 && 3 <= this.getMaxWireVersion());
    return hasExplain;
};

/**
 * {String} Returns the current mode set. Will be commands/legacy/compatibility
 *
 * Sends isMaster to determine if the connection is capable of using bulk write operations, and
 * caches the result.
 */

Monger.prototype.writeMode = function() {

    if ('_writeMode' in this) {
        return this._writeMode;
    }

    // get default from shell params
    if (_writeMode)
        this._writeMode = _writeMode();

    // can't use "commands" mode unless server version is good.
    if (this.hasWriteCommands()) {
        // good with whatever is already set
    } else if (this._writeMode == "commands") {
        this._writeMode = "compatibility";
    }

    return this._writeMode;
};

/**
 * Returns true if the shell is configured to use find/getMore commands rather than the C++ client.
 *
 * Currently, the C++ client will always use OP_QUERY find and OP_GET_MORE.
 */
Monger.prototype.useReadCommands = function() {
    return (this.readMode() === "commands");
};

/**
 * For testing, forces the shell to use the readMode specified in 'mode'. Must be either "commands"
 * (use the find/getMore commands), "legacy" (use legacy OP_QUERY/OP_GET_MORE wire protocol reads),
 * or "compatibility" (auto-detect mode based on wire version).
 */
Monger.prototype.forceReadMode = function(mode) {
    if (mode !== "commands" && mode !== "compatibility" && mode !== "legacy") {
        throw new Error("Mode must be one of {commands, compatibility, legacy}, but got: " + mode);
    }

    this._readMode = mode;
};

/**
 * Get the readMode string (either "commands" for find/getMore commands, "legacy" for OP_QUERY find
 * and OP_GET_MORE, or "compatibility" for detecting based on wire version).
 */
Monger.prototype.readMode = function() {
    // Get the readMode from the shell params if we don't have one yet.
    if (typeof _readMode === "function" && !this.hasOwnProperty("_readMode")) {
        this._readMode = _readMode();
    }

    if (this.hasOwnProperty("_readMode") && this._readMode !== "compatibility") {
        // We already have determined our read mode. Just return it.
        return this._readMode;
    } else {
        // We're in compatibility mode. Determine whether the server supports the find/getMore
        // commands. If it does, use commands mode. If not, degrade to legacy mode.
        try {
            var hasReadCommands = (this.getMinWireVersion() <= 4 && 4 <= this.getMaxWireVersion());
            if (hasReadCommands) {
                this._readMode = "commands";
            } else {
                this._readMode = "legacy";
            }
        } catch (e) {
            // We failed trying to determine whether the remote node supports the find/getMore
            // commands. In this case, we keep _readMode as "compatibility" and the shell should
            // issue legacy reads. Next time around we will issue another isMaster to try to
            // determine the readMode decisively.
        }
    }

    return this._readMode;
};

//
// Write Concern can be set at the connection level, and is used for all write operations unless
// overridden at the collection level.
//

Monger.prototype.setWriteConcern = function(wc) {
    if (wc instanceof WriteConcern) {
        this._writeConcern = wc;
    } else {
        this._writeConcern = new WriteConcern(wc);
    }
};

Monger.prototype.getWriteConcern = function() {
    return this._writeConcern;
};

Monger.prototype.unsetWriteConcern = function() {
    delete this._writeConcern;
};

Monger.prototype.advanceClusterTime = function(newTime) {
    if (!newTime.hasOwnProperty("clusterTime")) {
        throw new Error("missing clusterTime field in setClusterTime argument");
    }

    if (typeof this._clusterTime === "object" && this._clusterTime !== null) {
        this._clusterTime =
            (bsonWoCompare({_: this._clusterTime.clusterTime}, {_: newTime.clusterTime}) >= 0)
            ? this._clusterTime
            : newTime;
    } else {
        this._clusterTime = newTime;
    }
};

Monger.prototype.resetClusterTime_forTesting = function() {
    delete this._clusterTime;
};

Monger.prototype.getClusterTime = function() {
    return this._clusterTime;
};

Monger.prototype.startSession = function startSession(options = {}) {
    // Set retryWrites if not already set on options.
    if (!options.hasOwnProperty("retryWrites") && this.hasOwnProperty("_retryWrites")) {
        options.retryWrites = this._retryWrites;
    }
    const newDriverSession = new DriverSession(this, options);

    // Only log this message if we are running a test
    if (typeof TestData === "object" && TestData.testName) {
        jsTest.log("New session started with sessionID: " +
                   tojsononeline(newDriverSession.getSessionId()) + " and options: " +
                   tojsononeline(options));
    }

    return newDriverSession;
};

Monger.prototype._getDefaultSession = function getDefaultSession() {
    // We implicitly associate a Monger connection object with a real session so all requests include
    // a logical session id. These implicit sessions are intentionally not causally consistent. If
    // implicit sessions have been globally disabled, a dummy session is used instead of a real one.
    if (!this.hasOwnProperty("_defaultSession")) {
        if (_shouldUseImplicitSessions()) {
            try {
                this._defaultSession = this.startSession({causalConsistency: false});
            } catch (e) {
                if (e instanceof DriverSession.UnsupportedError) {
                    chatty("WARNING: No implicit session: " + e.message);
                    this._setDummyDefaultSession();
                } else {
                    print("ERROR: Implicit session failed: " + e.message);
                    throw(e);
                }
            }
        } else {
            this._setDummyDefaultSession();
        }
        this._defaultSession._isExplicit = false;
    }
    return this._defaultSession;
};

Monger.prototype._setDummyDefaultSession = function setDummyDefaultSession() {
    this._defaultSession = new _DummyDriverSession(this);
};

Monger.prototype.isCausalConsistency = function isCausalConsistency() {
    if (!this.hasOwnProperty("_causalConsistency")) {
        this._causalConsistency = false;
    }
    return this._causalConsistency;
};

Monger.prototype.setCausalConsistency = function setCausalConsistency(causalConsistency = true) {
    this._causalConsistency = causalConsistency;
};

Monger.prototype.waitForClusterTime = function waitForClusterTime(maxRetries = 10) {
    let isFirstTime = true;
    let count = 0;
    while (count < maxRetries) {
        if (typeof this._clusterTime === "object" && this._clusterTime !== null) {
            if (this._clusterTime.hasOwnProperty("signature") &&
                this._clusterTime.signature.keyId > 0) {
                return;
            }
        }
        if (isFirstTime) {
            isFirstTime = false;
        } else {
            sleep(500);
        }
        count++;
        this.adminCommand({"ping": 1});
    }
    throw new Error("failed waiting for non default clusterTime");
};

/**
 * Given the options object for a 'watch' helper, determines which options apply to the change
 * stream stage, and which apply to the aggregate overall. Returns two objects: the change
 * stream stage specification and the options for the aggregate command, respectively.
 */
Monger.prototype._extractChangeStreamOptions = function(options) {
    options = options || {};
    assert(options instanceof Object, "'options' argument must be an object");

    let changeStreamOptions = {fullDocument: options.fullDocument || "default"};
    delete options.fullDocument;

    if (options.hasOwnProperty("resumeAfter")) {
        changeStreamOptions.resumeAfter = options.resumeAfter;
        delete options.resumeAfter;
    }

    if (options.hasOwnProperty("startAfter")) {
        changeStreamOptions.startAfter = options.startAfter;
        delete options.startAfter;
    }

    if (options.hasOwnProperty("startAtOperationTime")) {
        changeStreamOptions.startAtOperationTime = options.startAtOperationTime;
        delete options.startAtOperationTime;
    }

    return [{$changeStream: changeStreamOptions}, options];
};

Monger.prototype.watch = function(pipeline, options) {
    pipeline = pipeline || [];
    assert(pipeline instanceof Array, "'pipeline' argument must be an array");

    let changeStreamStage;
    [changeStreamStage, aggOptions] = this._extractChangeStreamOptions(options);
    changeStreamStage.$changeStream.allChangesForCluster = true;
    pipeline.unshift(changeStreamStage);
    return this.getDB("admin")._runAggregate({aggregate: 1, pipeline: pipeline}, aggOptions);
};
