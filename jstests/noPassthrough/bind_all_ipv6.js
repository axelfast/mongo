// Startup with --bind_ip_all and --ipv6 should not fail with address already in use.

(function() {
    'use strict';

    const monger = MongoRunner.runMongod({ipv6: "", bind_ip_all: ""});
    assert(monger !== null, "Database is not running");
    assert.commandWorked(monger.getDB("test").isMaster(), "isMaster failed");
    MongoRunner.stopMongod(monger);
}());
