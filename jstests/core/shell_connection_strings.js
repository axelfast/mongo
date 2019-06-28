// Test monger shell connect strings.
// @tags: [
//   uses_multiple_connections,
// ]
(function() {
    'use strict';

    const mongerd = new MongoURI(db.getMongo().host).servers[0];
    const host = mongerd.host;
    const port = mongerd.port;

    function testConnect(ok, ...args) {
        const exitCode = runMongoProgram('monger', '--eval', ';', ...args);
        if (ok) {
            assert.eq(exitCode, 0, "failed to connect with `" + args.join(' ') + "`");
        } else {
            assert.neq(
                exitCode, 0, "unexpectedly succeeded connecting with `" + args.join(' ') + "`");
        }
    }

    testConnect(true, `${host}:${port}`);
    testConnect(true, `${host}:${port}/test`);
    testConnect(true, `${host}:${port}/admin`);
    testConnect(true, host, '--port', port);
    testConnect(true, '--host', host, '--port', port, 'test');
    testConnect(true, '--host', host, '--port', port, 'admin');
    testConnect(true, `mongerdb://${host}:${port}/test`);
    testConnect(true, `mongerdb://${host}:${port}/test?connectTimeoutMS=10000`);

    // if a full URI is provided, you cannot also specify host or port
    testConnect(false, `${host}/test`, '--port', port);
    testConnect(false, `mongerdb://${host}:${port}/test`, '--port', port);
    testConnect(false, `mongerdb://${host}:${port}/test`, '--host', host);
    testConnect(false, `mongerdb://${host}:${port}/test`, '--host', host, '--port', port);
})();
