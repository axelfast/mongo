/**
 * Test the backup/restore process:
 * - 3 node replica set
 * - Monger CRUD client
 * - Monger FSM client
 * - fsyncLock (or stop) Secondary
 * - cp (or rsync) DB files
 * - fsyncUnlock (or start) Secondary
 * - Start mongerd as hidden secondary
 * - Wait until new hidden node becomes secondary
 *
 * Some methods for backup used in this test checkpoint the files in the dbpath. This technique will
 * not work for ephemeral storage engines, as they do not store any data in the dbpath.
 * @tags: [requires_persistence, requires_wiredtiger]
 */

load("jstests/noPassthrough/libs/backup_restore.js");

(function() {
    "use strict";

    // Grab the storage engine, default is wiredTiger
    var storageEngine = jsTest.options().storageEngine || "wiredTiger";

    // Skip this test if not running with the "wiredTiger" storage engine.
    if (storageEngine !== 'wiredTiger') {
        jsTest.log('Skipping test because storageEngine is not "wiredTiger"');
        return;
    }

    // Skip this test if running with --nojournal and WiredTiger.
    if (jsTest.options().noJournal) {
        print("Skipping test because running WiredTiger without journaling isn't a valid" +
              " replica set configuration");
        return;
    }

    // if rsync is not available on the host, then this test is skipped
    if (!runProgram('bash', '-c', 'which rsync')) {
        new BackupRestoreTest({backup: 'rolling', clientTime: 30000}).run();
    } else {
        jsTestLog("Skipping test for " + storageEngine + ' rolling');
    }
}());
