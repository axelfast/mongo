// The mongerd process should always create a mongerd.lock file in the data directory
// containing the process ID regardless of the storage engine requested.

(function() {
    // Ensures that mongerd.lock exists and returns size of file.
    function getMongodLockFileSize(dir) {
        var files = listFiles(dir);
        for (var i in files) {
            var file = files[i];
            if (!file.isDirectory && file.baseName == 'mongerd.lock') {
                return file.size;
            }
        }
        assert(false, 'mongerd.lock not found in data directory ' + dir);
    }

    var baseName = "jstests_lock_file";
    var dbpath = MongoRunner.dataPath + baseName + '/';

    // Test framework will append --storageEngine command line option.
    var mongerd = MongoRunner.runMongod({dbpath: dbpath});
    assert.neq(0,
               getMongodLockFileSize(dbpath),
               'mongerd.lock should not be empty while server is running');

    MongoRunner.stopMongod(mongerd);

    // mongerd.lock must be empty after shutting server down.
    assert.eq(
        0, getMongodLockFileSize(dbpath), 'mongerd.lock not truncated after shutting server down');
}());
