/**
 * This test is for SERVER-29453: Renaming the admin.system.version collection
 * or removing the FCV document should not be allowed.
 */
(function() {
    'use strict';

    let standalone = MongerRunner.runMongerd();
    assert.neq(null, standalone, 'mongerd was unable to start up');
    let adminDB = standalone.getDB('admin');

    // Renaming the collection or deleting the document should fail.
    assert.commandFailedWithCode(
        adminDB.runCommand(
            {renameCollection: 'admin.system.version', to: 'admin.dummy.collection'}),
        ErrorCodes.IllegalOperation);
    assert.writeErrorWithCode(adminDB.system.version.remove({}), 40670);
    MongerRunner.stopMongerd(standalone);
})();
