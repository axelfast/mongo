// Test validation of connection strings passed to the JavaScript "connect()" function.
// @tags: [
//   uses_multiple_connections,
// ]
// Related to SERVER-8030.

port = "27017";

if (db.getMonger().host.indexOf(":") >= 0) {
    var idx = db.getMonger().host.indexOf(":");
    port = db.getMonger().host.substring(idx + 1);
}

var goodStrings = [
    "localhost:" + port + "/test",
    "127.0.0.1:" + port + "/test",
    "127.0.0.1:" + port + "/",
];

var missingConnString = /^Missing connection string$/;
var incorrectType = /^Incorrect type/;
var emptyConnString = /^Empty connection string$/;
var badHost = /^Failed to parse mongerdb/;
var emptyHost = /^Empty host component/;
var noPort = /^No digits/;
var invalidPort = /^Port number \d+ out of range/;
var multipleColon = /^More than one ':' detected./;
var noReplSet = /^connect failed to replica set/;
var badStrings = [
    {s: undefined, r: missingConnString},
    {s: 7, r: incorrectType},
    {s: null, r: incorrectType},
    {s: "", r: emptyConnString},
    {s: "    ", r: emptyConnString},
    {s: ":", r: emptyHost},
    {s: "/", r: badHost},
    {s: "/test", r: badHost},
    {s: ":/", r: emptyHost},
    {s: ":/test", r: emptyHost},
    {s: "mongerdb://:" + port + "/", r: emptyHost},
    {s: "mongerdb://:" + port + "/test", r: emptyHost},
    {s: "mongerdb://localhost:/test", r: noPort},
    {s: "mongerdb://127.0.0.1:/test", r: noPort},
    {s: "mongerdb://127.0.0.1:cat/test", c: ErrorCodes.FailedToParse},
    {s: "mongerdb://127.0.0.1:1cat/test", c: ErrorCodes.FailedToParse},
    {s: "mongerdb://127.0.0.1:123456/test", r: invalidPort},
    {s: "mongerdb://127.0.0.1:65536/test", r: invalidPort},
    {s: "mongerdb://::1:65536/test", r: multipleColon},
    {s: "mongerdb://::1:" + port + "/", r: multipleColon}
];

function testGoodAsURI(i, uri) {
    uri = "mongerdb://" + uri;
    print("\nTesting good uri " + i + " (\"" + uri + "\") ...");
    var gotException = false;
    var exception;
    try {
        var m_uri = MongerURI(uri);
        var connectDB = connect(uri);
        connectDB = null;
    } catch (e) {
        gotException = true;
        exception = e;
    }
    if (!gotException) {
        print("Good uri " + i + " (\"" + uri + "\") correctly validated");
        return;
    }
    var message = "FAILED to correctly validate goodString " + i + " (\"" + uri +
        "\"):  exception was \"" + tojson(exception) + "\"";
    doassert(message);
}

function testBad(i, connectionString, errorRegex, errorCode) {
    print("\nTesting bad connection string " + i + " (\"" + connectionString + "\") ...");
    var gotException = false;
    var gotCorrectErrorText = false;
    var gotCorrectErrorCode = false;
    var exception;
    try {
        var connectDB = connect(connectionString);
        connectDB = null;
    } catch (e) {
        gotException = true;
        exception = e;
        if (errorRegex && errorRegex.test(e.message)) {
            gotCorrectErrorText = true;
        }
        if (errorCode == e.code) {
            gotCorrectErrorCode = true;
        }
    }
    if (gotCorrectErrorText || gotCorrectErrorCode) {
        print("Bad connection string " + i + " (\"" + connectionString +
              "\") correctly rejected:\n" + tojson(exception));
        return;
    }
    var message = "FAILED to generate correct exception for badString " + i + " (\"" +
        connectionString + "\"): ";
    if (gotException) {
        message += "exception was \"" + tojson(exception) + "\", it should have matched \"" +
            errorRegex.toString() + "\"";
    } else {
        message += "no exception was thrown";
    }
    doassert(message);
}

var i;
jsTest.log("TESTING " + goodStrings.length + " good connection strings");
for (i = 0; i < goodStrings.length; ++i) {
    testGoodAsURI(i, goodStrings[i]);
}

jsTest.log("TESTING " + badStrings.length + " bad connection strings");
for (i = 0; i < badStrings.length; ++i) {
    testBad(i, badStrings[i].s, badStrings[i].r, badStrings[i].c);
}

jsTest.log("SUCCESSFUL test completion");
