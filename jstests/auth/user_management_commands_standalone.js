(function() {
    'use strict';

    load('jstests/auth/user_management_commands_lib.js');

    var conn = MongerRunner.runMongerd({auth: '', useHostname: false});
    runAllUserManagementCommandsTests(conn);
    MongerRunner.stopMongerd(conn);
})();
