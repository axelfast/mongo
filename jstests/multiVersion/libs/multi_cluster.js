//
// MultiVersion utility functions for clusters
//

/**
 * Restarts the specified binaries in options with the specified binVersion.
 * Note: this does not perform any upgrade operations.
 *
 * @param binVersion {string}
 * @param options {Object} format:
 *
 * {
 *     upgradeShards: <bool>, // defaults to true
 *     upgradeConfigs: <bool>, // defaults to true
 *     upgradeMongos: <bool>, // defaults to true
 * }
 */
ShardingTest.prototype.upgradeCluster = function(binVersion, options) {
    options = options || {};
    if (options.upgradeShards == undefined)
        options.upgradeShards = true;
    if (options.upgradeConfigs == undefined)
        options.upgradeConfigs = true;
    if (options.upgradeMongos == undefined)
        options.upgradeMongos = true;

    var upgradedSingleShards = [];

    if (options.upgradeConfigs) {
        // Upgrade config servers if they aren't already upgraded shards
        var numConfigs = this._configServers.length;

        for (var i = 0; i < numConfigs; i++) {
            var configSvr = this._configServers[i];

            if (configSvr.host in upgradedSingleShards) {
                configSvr = upgradedSingleShards[configSvr.host];
            } else {
                MongoRunner.stopMongod(configSvr);
                configSvr = MongoRunner.runMongod(
                    {restart: configSvr, binVersion: binVersion, appendOptions: true});
            }

            this["config" + i] = this["c" + i] = this._configServers[i] = configSvr;
        }
    }

    if (options.upgradeShards) {
        var numShards = this._connections.length;

        // Upgrade shards
        for (var i = 0; i < numShards; i++) {
            if (this._rs && this._rs[i]) {
                // Upgrade replica set
                var rst = this._rs[i].test;
                rst.upgradeSet({binVersion: binVersion});
            } else {
                // Upgrade shard
                var shard = this._connections[i];
                MongoRunner.stopMongod(shard);
                shard = MongoRunner.runMongod(
                    {restart: shard, binVersion: binVersion, appendOptions: true});

                upgradedSingleShards[shard.host] = shard;
                this["shard" + i] = this["d" + i] = this._connections[i] = shard;
            }
        }
    }

    if (options.upgradeMongos) {
        // Upgrade all mongers hosts if specified
        var numMongoses = this._mongers.length;

        for (var i = 0; i < numMongoses; i++) {
            var mongers = this._mongers[i];
            MongoRunner.stopMongos(mongers);

            mongers = MongoRunner.runMongos(
                {restart: mongers, binVersion: binVersion, appendOptions: true});

            this["s" + i] = this._mongers[i] = mongers;
            if (i == 0)
                this.s = mongers;
        }

        this.config = this.s.getDB("config");
        this.admin = this.s.getDB("admin");
    }
};

ShardingTest.prototype.restartMongoses = function() {

    var numMongoses = this._mongers.length;

    for (var i = 0; i < numMongoses; i++) {
        var mongers = this._mongers[i];

        MongoRunner.stopMongos(mongers);
        mongers = MongoRunner.runMongos({restart: mongers});

        this["s" + i] = this._mongers[i] = mongers;
        if (i == 0)
            this.s = mongers;
    }

    this.config = this.s.getDB("config");
    this.admin = this.s.getDB("admin");
};

ShardingTest.prototype.getMongosAtVersion = function(binVersion) {
    var mongerses = this._mongers;
    for (var i = 0; i < mongerses.length; i++) {
        try {
            var version = mongerses[i].getDB("admin").runCommand("serverStatus").version;
            if (version.indexOf(binVersion) == 0) {
                return mongerses[i];
            }
        } catch (e) {
            printjson(e);
            print(mongerses[i]);
        }
    }
};
