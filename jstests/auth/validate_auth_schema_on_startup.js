/**
 * This verifies that an invalid authSchema document causes MongerDB to fail to start, except in the
 * presence of startupAuthSchemaValidation=false.
 *
 * @tags: [requires_persistence]
 */
(function() {

    const dbpath = MongerRunner.dataPath + "validateAuthSchemaOnStartup/";
    resetDbpath(dbpath);
    const dbName = "validateAuthSchemaOnStartup";
    const authSchemaColl = "system.version";

    let mongerd = MongerRunner.runMongerd({dbpath: dbpath, auth: ""});
    let adminDB = mongerd.getDB('admin');

    // Create a user.
    adminDB.createUser(
        {user: "root", pwd: "root", roles: [{role: 'userAdminAnyDatabase', db: 'admin'}]});
    assert(adminDB.auth("root", "root"));

    MongerRunner.stopMongerd(mongerd);

    // Start without auth to corrupt the authSchema document.
    mongerd = MongerRunner.runMongerd({dbpath: dbpath, noCleanData: true});
    adminDB = mongerd.getDB('admin');

    let currentVersion = adminDB[authSchemaColl].findOne({_id: 'authSchema'}).currentVersion;

    // Invalidate the authSchema document.
    assert.commandWorked(
        adminDB[authSchemaColl].update({_id: 'authSchema'}, {currentVersion: 'asdf'}));
    MongerRunner.stopMongerd(mongerd);

    // Confirm start up fails, even without --auth.
    assert.eq(null, MongerRunner.runMongerd({dbpath: dbpath, noCleanData: true}));

    // Confirm startup works with the flag to disable validation so the document can be repaired.
    mongerd = MongerRunner.runMongerd(
        {dbpath: dbpath, noCleanData: true, setParameter: "startupAuthSchemaValidation=false"});
    adminDB = mongerd.getDB('admin');
    assert.commandWorked(
        adminDB[authSchemaColl].update({_id: 'authSchema'}, {currentVersion: currentVersion}));
    MongerRunner.stopMongerd(mongerd);

    // Confirm everything is normal again.
    mongerd = MongerRunner.runMongerd({dbpath: dbpath, noCleanData: true, auth: ""});
    adminDB = mongerd.getDB('admin');
    assert(adminDB.auth("root", "root"));

    MongerRunner.stopMongerd(mongerd);
})();
