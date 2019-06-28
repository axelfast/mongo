/*
 * This test makes sure that the log files created by the server correctly honor the server's umask
 * as set in SERVER-22829
 *
 * @tags: [ requires_wiredtiger ]
 */
(function() {
    'use strict';
    // We only test this on POSIX since that's the only platform where umasks make sense
    if (_isWindows()) {
        return;
    }

    const oldUmask = new Number(umask(0));
    jsTestLog("Setting umask to really permissive 000 mode, old mode was " + oldUmask.toString(8));

    const defaultUmask = Number.parseInt("600", 8);
    const permissiveUmask = Number.parseInt("666", 8);

    // Any files that have some explicit permissions set on them should be added to this list
    const exceptions = [
        // The lock file gets created with explicit 644 permissions
        'mongerd.lock',
        // Mobile se files get created with 644 permissions when honoring the system umask
        'mobile.sqlite',
        'mobile.sqlite-shm',
        'mobile.sqlite-wal',
    ];

    let mongerdOptions = MongerRunner.mongerdOptions({
        useLogFiles: true,
        cleanData: true,
    });

    if (buildInfo()["modules"].some((mod) => {
            return mod == "enterprise";
        })) {
        mongerdOptions.auditDestination = "file";
        mongerdOptions.auditPath = mongerdOptions.dbpath + "/audit.log";
        mongerdOptions.auditFormat = "JSON";
    }

    const checkMask = (topDir, expected, honoringUmask) => {
        const maybeNot = honoringUmask ? "" : " not";
        const processDirectory = (dir) => {
            jsTestLog(`Checking ${dir}`);
            ls(dir).forEach((file) => {
                if (file.endsWith("/")) {
                    return processDirectory(file);
                } else if (exceptions.some((exception) => {
                               return file.endsWith(exception);
                           })) {
                    return;
                }
                const mode = new Number(getFileMode(file));
                const modeStr = mode.toString(8);
                const msg = `Mode for ${file} is ${modeStr} when${maybeNot} honoring system umask`;
                assert.eq(mode.valueOf(), expected, msg);
            });
        };

        processDirectory(topDir);
    };

    // First we start up the mongerd normally, all the files except mongerd.lock should have the mode
    // 0600
    let conn = MongerRunner.runMongerd(mongerdOptions);
    MongerRunner.stopMongerd(conn);
    checkMask(conn.fullOptions.dbpath, defaultUmask, false);

    // Restart the mongerd with honorSystemUmask, all files should have the mode 0666
    mongerdOptions.setParameter = {honorSystemUmask: true};
    conn = MongerRunner.runMongerd(mongerdOptions);
    MongerRunner.stopMongerd(conn);
    checkMask(conn.fullOptions.dbpath, permissiveUmask, false);

    umask(oldUmask.valueOf());
})();
