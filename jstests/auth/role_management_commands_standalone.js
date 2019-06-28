(function() {
    'use strict';

    load('jstests/auth/role_management_commands_lib.js');

    var conn = MongerRunner.runMongerd({auth: '', useHostname: false});
    runAllRoleManagementCommandsTests(conn);
    MongerRunner.stopMongerd(conn);
})();
