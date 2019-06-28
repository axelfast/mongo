'use strict';

/**
 * Returns true if the process is a mongers, and false otherwise.
 *
 */
function isMongers(db) {
    // Run isMaster directly on the database's session's client to bypass any session machinery.
    const res = assert.commandWorked(db.getSession().getClient().adminCommand('ismaster'));
    return 'isdbgrid' === res.msg;
}

/**
 * Returns true if the process is a mongerd, and false otherwise.
 *
 */
function isMongerd(db) {
    return !isMongers(db);
}

/**
 * Returns true if the process is a mongerd configsvr, and false otherwise.
 *
 */
function isMongerdConfigsvr(db) {
    if (!isMongerd(db)) {
        return false;
    }
    var res = db.adminCommand('getCmdLineOpts');
    assert.commandWorked(res);

    return res.parsed && res.parsed.sharding && res.parsed.sharding.clusterRole === 'configsvr';
}

/**
 * Returns the name of the current storage engine.
 *
 * Throws an error if db is connected to a mongers, or if there is no reported storage engine.
 */
function getStorageEngineName(db) {
    var status = db.serverStatus();
    assert.commandWorked(status);

    assert(isMongerd(db), 'no storage engine is reported when connected to mongers');
    assert.neq(
        'undefined', typeof status.storageEngine, 'missing storage engine info in server status');

    return status.storageEngine.name;
}

/**
 * Returns true if the current storage engine is wiredTiger, and false otherwise.
 */
function isWiredTiger(db) {
    return getStorageEngineName(db) === 'wiredTiger';
}

/**
 * Returns true if the current storage engine is ephemeralForTest, and false otherwise.
 */
function isEphemeralForTest(db) {
    return getStorageEngineName(db) === 'ephemeralForTest';
}

/**
 * Returns true if the current storage engine is ephemeral, and false otherwise.
 */
function isEphemeral(db) {
    var engine = getStorageEngineName(db);
    return (engine === 'inMemory') || (engine === 'ephemeralForTest');
}

/**
 * Returns true if the current storage engine supports document-level concurrency, and false
 * otherwise.
 */
function supportsDocumentLevelConcurrency(db) {
    var engine = getStorageEngineName(db);
    return ['wiredTiger', 'rocksdb', 'inMemory'].indexOf(engine) !== -1;
}

/**
 * Returns true if the current storage engine supports committed reads.
 *
 * Throws an error if db is connected to a mongers, or if there is no reported storage engine.
 */
function supportsCommittedReads(db) {
    var status = db.serverStatus();
    assert.commandWorked(status);

    assert(isMongerd(db), 'no storage engine is reported when connected to mongers');
    assert.neq(
        'undefined', typeof status.storageEngine, 'missing storage engine info in server status');

    return status.storageEngine.supportsCommittedReads;
}
