// Tests the parsing of the timeZoneInfo parameter.
(function() {
    // Test that a bad file causes startup to fail.
    let conn = MongerRunner.runMongerd({timeZoneInfo: "jstests/libs/config_files/bad_timezone_info"});
    assert.eq(conn, null, "expected launching mongerd with bad timezone rules to fail");
    assert.neq(-1, rawMongerProgramOutput().indexOf("Fatal assertion 40475"));

    // Test that a non-existent directory causes startup to fail.
    conn = MongerRunner.runMongerd({timeZoneInfo: "jstests/libs/config_files/missing_directory"});
    assert.eq(conn, null, "expected launching mongerd with bad timezone rules to fail");

    // Look for either old or new error message
    assert(rawMongerProgramOutput().indexOf("Failed to create service context") != -1 ||
           rawMongerProgramOutput().indexOf("Failed global initialization") != -1);

    // Test that startup can succeed with a good file.
    conn = MongerRunner.runMongerd({timeZoneInfo: "jstests/libs/config_files/good_timezone_info"});
    assert.neq(conn, null, "expected launching mongerd with good timezone rules to succeed");
    MongerRunner.stopMongerd(conn);
}());
