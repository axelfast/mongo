/**
 * Helpers for verifying versions of started MongerDB processes.
 */

var Monger, assert;
(function() {
    "use strict";
    Monger.prototype.getBinVersion = function() {
        var result = this.getDB("admin").runCommand({serverStatus: 1});
        return result.version;
    };

    // Checks that our mongerdb process is of a certain version
    assert.binVersion = function(monger, version) {
        var currVersion = monger.getBinVersion();
        assert(MongerRunner.areBinVersionsTheSame(MongerRunner.getBinVersionFor(currVersion),
                                                 MongerRunner.getBinVersionFor(version)),
               "version " + version + " (" + MongerRunner.getBinVersionFor(version) + ")" +
                   " is not the same as " + MongerRunner.getBinVersionFor(currVersion));
    };

    // Compares an array of desired versions and an array of found versions,
    // looking for versions not found
    assert.allBinVersions = function(versionsWanted, versionsFound) {

        for (var i = 0; i < versionsWanted.length; i++) {
            var version = versionsWanted[i];
            var found = false;
            for (var j = 0; j < versionsFound.length; j++) {
                if (MongerRunner.areBinVersionsTheSame(version, versionsFound[j])) {
                    found = true;
                    break;
                }
            }

            assert(found,
                   "could not find version " + version + " (" +
                       MongerRunner.getBinVersionFor(version) + ")" + " in " + versionsFound);
        }
    };

}());
