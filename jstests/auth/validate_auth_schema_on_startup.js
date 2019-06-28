/**
 * This verifies that an invalid authSchema document causes MongoDB to fail to start, except in the
 * presence of startupAuthSchemaValidation=false.
 *
 * @tags: [requires_persistence]
 */
(function() {

    const dbpath = MongoRunner.dataPath + "validateAuthSchemaOnStartup/";
    resetDbpath(dbpath);
    const dbName = "validateAuthSchemaOnStartup";
    const authSchemaColl = "system.version";

    let mongerd = MongoRunner.runMongod({dbpath: dbpath, auth: ""});
    let adminDB = mongerd.getDB('admin');

    // Create a user.
    adminDB.createUser(
        {user: "root", pwd: "root", roles: [{role: 'userAdminAnyDatabase', db: 'admin'}]});
    assert(adminDB.auth("root", "root"));

    MongoRunner.stopMongod(mongerd);

    // Start without auth to corrupt the authSchema document.
    mongerd = MongoRunner.runMongod({dbpath: dbpath, noCleanData: true});
    adminDB = mongerd.getDB('admin');

    let currentVersion = adminDB[authSchemaColl].findOne({_id: 'authSchema'}).currentVersion;

    // Invalidate the authSchema document.
    assert.commandWorked(
        adminDB[authSchemaColl].update({_id: 'authSchema'}, {currentVersion: 'asdf'}));
    MongoRunner.stopMongod(mongerd);

    // Confirm start up fails, even without --auth.
    assert.eq(null, MongoRunner.runMongod({dbpath: dbpath, noCleanData: true}));

    // Confirm startup works with the flag to disable validation so the document can be repaired.
    mongerd = MongoRunner.runMongod(
        {dbpath: dbpath, noCleanData: true, setParameter: "startupAuthSchemaValidation=false"});
    adminDB = mongerd.getDB('admin');
    assert.commandWorked(
        adminDB[authSchemaColl].update({_id: 'authSchema'}, {currentVersion: currentVersion}));
    MongoRunner.stopMongod(mongerd);

    // Confirm everything is normal again.
    mongerd = MongoRunner.runMongod({dbpath: dbpath, noCleanData: true, auth: ""});
    adminDB = mongerd.getDB('admin');
    assert(adminDB.auth("root", "root"));

    MongoRunner.stopMongod(mongerd);
})();
