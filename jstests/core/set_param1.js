// @tags: [
//   assumes_superuser_permissions,
//   does_not_support_stepdowns,
// ]

// Tests for accessing logLevel server parameter using getParameter/setParameter commands
// and shell helpers.

old = db.adminCommand({"getParameter": "*"});
// the first time getParameter sends a request to with a shardingTaskExecutor and this sets an
// operationTime. The following commands do not use shardingTaskExecutor.
delete old["operationTime"];
delete old["$clusterTime"];
tmp1 = db.adminCommand({"setParameter": 1, "logLevel": 5});
tmp2 = db.adminCommand({"setParameter": 1, "logLevel": old.logLevel});
now = db.adminCommand({"getParameter": "*"});
delete now["operationTime"];
delete now["$clusterTime"];

assert.eq(old, now, "A");
assert.eq(old.logLevel, tmp1.was, "B");
assert.eq(5, tmp2.was, "C");

//
// component verbosity
//

// verbosity for log component hierarchy
printjson(old.logComponentVerbosity);
assert.neq(undefined, old.logComponentVerbosity, "log component verbosity not available");
assert.eq(old.logLevel,
          old.logComponentVerbosity.verbosity,
          "default component verbosity should match logLevel");
assert.neq(undefined,
           old.logComponentVerbosity.storage.journal.verbosity,
           "journal verbosity not available");

// Non-object log component verbosity should be rejected.
assert.commandFailed(db.adminCommand({"setParameter": 1, logComponentVerbosity: "not an object"}));

// Non-numeric verbosity for component should be rejected.
assert.commandFailed(db.adminCommand(
    {"setParameter": 1, logComponentVerbosity: {storage: {journal: {verbosity: "not a number"}}}}));

// Invalid component shall be rejected
assert.commandFailed(
    db.adminCommand({"setParameter": 1, logComponentVerbosity: {NoSuchComponent: {verbosity: 2}}}));

// Set multiple component log levels at once.
(function() {
    assert.commandWorked(db.adminCommand({
        "setParameter": 1,
        logComponentVerbosity: {
            verbosity: 2,
            accessControl: {verbosity: 0},
            storage: {verbosity: 3, journal: {verbosity: 5}}
        }
    }));

    var result =
        assert.commandWorked(db.adminCommand({"getParameter": 1, logComponentVerbosity: 1}))
            .logComponentVerbosity;

    assert.eq(2, result.verbosity);
    assert.eq(0, result.accessControl.verbosity);
    assert.eq(3, result.storage.verbosity);
    assert.eq(5, result.storage.journal.verbosity);
})();

// Set multiple component log levels at once.
// Unrecognized field names not mapping to a log component shall be rejected
// No changes shall apply.
(function() {
    assert.commandFailed(db.adminCommand({
        "setParameter": 1,
        logComponentVerbosity: {
            verbosity: 6,
            accessControl: {verbosity: 5},
            storage: {verbosity: 4, journal: {verbosity: 6}},
            NoSuchComponent: {verbosity: 2},
            extraField: 123
        }
    }));

    var result =
        assert.commandWorked(db.adminCommand({"getParameter": 1, logComponentVerbosity: 1}))
            .logComponentVerbosity;

    assert.eq(2, result.verbosity);
    assert.eq(0, result.accessControl.verbosity);
    assert.eq(3, result.storage.verbosity);
    assert.eq(5, result.storage.journal.verbosity);
})();

// Clear verbosity for default and journal.
(function() {
    assert.commandWorked(db.adminCommand({
        "setParameter": 1,
        logComponentVerbosity: {verbosity: -1, storage: {journal: {verbosity: -1}}}
    }));

    var result =
        assert.commandWorked(db.adminCommand({"getParameter": 1, logComponentVerbosity: 1}))
            .logComponentVerbosity;

    assert.eq(0, result.verbosity);
    assert.eq(0, result.accessControl.verbosity);
    assert.eq(3, result.storage.verbosity);
    assert.eq(-1, result.storage.journal.verbosity);
})();

// Set accessControl verbosity using numerical level instead of
// subdocument with 'verbosity' field.
(function() {
    assert.commandWorked(
        db.adminCommand({"setParameter": 1, logComponentVerbosity: {accessControl: 5}}));

    var result =
        assert.commandWorked(db.adminCommand({"getParameter": 1, logComponentVerbosity: 1}))
            .logComponentVerbosity;

    assert.eq(5, result.accessControl.verbosity);
})();

// Restore old verbosity values.
assert.commandWorked(
    db.adminCommand({"setParameter": 1, logComponentVerbosity: old.logComponentVerbosity}));

var isMongers = (db.isMaster().msg === 'isdbgrid');
if (!isMongers) {
    //
    // oplogFetcherSteadyStateMaxFetcherRestarts
    //
    var origRestarts = assert
                           .commandWorked(db.adminCommand(
                               {getParameter: 1, oplogFetcherSteadyStateMaxFetcherRestarts: 1}))
                           .oplogFetcherSteadyStateMaxFetcherRestarts;
    assert.gte(origRestarts,
               0,
               'default value of oplogFetcherSteadyStateMaxFetcherRestarts cannot be negative');
    assert.commandFailedWithCode(
        db.adminCommand({setParameter: 1, oplogFetcherSteadyStateMaxFetcherRestarts: -1}),
        ErrorCodes.BadValue,
        'server should reject negative values for oplogFetcherSteadyStateMaxFetcherRestarts');
    assert.commandWorked(
        db.adminCommand({setParameter: 1, oplogFetcherSteadyStateMaxFetcherRestarts: 0}));
    assert.commandWorked(db.adminCommand(
        {setParameter: 1, oplogFetcherSteadyStateMaxFetcherRestarts: origRestarts + 20}));
    assert.eq(origRestarts + 20,
              assert
                  .commandWorked(db.adminCommand(
                      {getParameter: 1, oplogFetcherSteadyStateMaxFetcherRestarts: 1}))
                  .oplogFetcherSteadyStateMaxFetcherRestarts);
    // Restore original value.
    assert.commandWorked(db.adminCommand(
        {setParameter: 1, oplogFetcherSteadyStateMaxFetcherRestarts: origRestarts}));

    //
    // oplogFetcherInitialSyncStateMaxFetcherRestarts
    //
    origRestarts = assert
                       .commandWorked(db.adminCommand(
                           {getParameter: 1, oplogFetcherInitialSyncMaxFetcherRestarts: 1}))
                       .oplogFetcherInitialSyncMaxFetcherRestarts;
    assert.gte(origRestarts,
               0,
               'default value of oplogFetcherSteadyStateMaxFetcherRestarts cannot be negative');
    assert.commandFailedWithCode(
        db.adminCommand({setParameter: 1, oplogFetcherInitialSyncMaxFetcherRestarts: -1}),
        ErrorCodes.BadValue,
        'server should reject negative values for oplogFetcherSteadyStateMaxFetcherRestarts');
    assert.commandWorked(
        db.adminCommand({setParameter: 1, oplogFetcherInitialSyncMaxFetcherRestarts: 0}));
    assert.commandWorked(db.adminCommand(
        {setParameter: 1, oplogFetcherInitialSyncMaxFetcherRestarts: origRestarts + 20}));
    assert.eq(origRestarts + 20,
              assert
                  .commandWorked(db.adminCommand(
                      {getParameter: 1, oplogFetcherInitialSyncMaxFetcherRestarts: 1}))
                  .oplogFetcherInitialSyncMaxFetcherRestarts);
    // Restore original value.
    assert.commandWorked(db.adminCommand(
        {setParameter: 1, oplogFetcherInitialSyncMaxFetcherRestarts: origRestarts}));
}
