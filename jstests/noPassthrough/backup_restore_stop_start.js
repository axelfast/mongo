/**
 * Test the backup/restore process:
 * - 3 node replica set
 * - Monger CRUD client
 * - Monger FSM client
 * - Stop Secondary
 * - cp DB files
 * - Start Secondary
 * - Start mongerd as hidden secondary
 * - Wait until new hidden node becomes secondary
 *
 * Some methods for backup used in this test checkpoint the files in the dbpath. This technique will
 * not work for ephemeral storage engines, as they do not store any data in the dbpath.
 * @tags: [requires_persistence, requires_replication]
 */

load("jstests/noPassthrough/libs/backup_restore.js");

(function() {
    "use strict";

    new BackupRestoreTest({backup: 'stopStart', clientTime: 30000}).run();
}());
