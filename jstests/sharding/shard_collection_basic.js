//
// Basic tests for shardCollection.
//

(function() {
    'use strict';

    var st = new ShardingTest({mongers: 1, shards: 2});
    var kDbName = 'db';
    var mongers = st.s0;

    function testAndClenaupWithKeyNoIndexFailed(keyDoc) {
        assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));

        var ns = kDbName + '.foo';
        assert.commandFailed(mongers.adminCommand({shardCollection: ns, key: keyDoc}));

        assert.eq(mongers.getDB('config').collections.count({_id: ns, dropped: false}), 0);
        assert.commandWorked(mongers.getDB(kDbName).dropDatabase());
    }

    function testAndClenaupWithKeyOK(keyDoc) {
        assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));
        assert.commandWorked(mongers.getDB(kDbName).foo.createIndex(keyDoc));

        var ns = kDbName + '.foo';
        assert.eq(mongers.getDB('config').collections.count({_id: ns, dropped: false}), 0);

        assert.commandWorked(mongers.adminCommand({shardCollection: ns, key: keyDoc}));

        assert.eq(mongers.getDB('config').collections.count({_id: ns, dropped: false}), 1);
        assert.commandWorked(mongers.getDB(kDbName).dropDatabase());
    }

    function testAndClenaupWithKeyNoIndexOK(keyDoc) {
        assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));

        var ns = kDbName + '.foo';
        assert.eq(mongers.getDB('config').collections.count({_id: ns, dropped: false}), 0);

        assert.commandWorked(mongers.adminCommand({shardCollection: ns, key: keyDoc}));

        assert.eq(mongers.getDB('config').collections.count({_id: ns, dropped: false}), 1);
        assert.commandWorked(mongers.getDB(kDbName).dropDatabase());
    }

    function getIndexSpecByName(coll, indexName) {
        var indexes = coll.getIndexes().filter(function(spec) {
            return spec.name === indexName;
        });
        assert.eq(1, indexes.length, 'index "' + indexName + '" not found"');
        return indexes[0];
    }

    // Fail if db is not sharded.
    assert.commandFailed(mongers.adminCommand({shardCollection: kDbName + '.foo', key: {_id: 1}}));

    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: 1, b: 1}));

    // Fail if db is not sharding enabled.
    assert.commandFailed(mongers.adminCommand({shardCollection: kDbName + '.foo', key: {_id: 1}}));

    assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));

    // Verify wrong arguments errors.
    assert.commandFailed(mongers.adminCommand({shardCollection: 'foo', key: {_id: 1}}));

    assert.commandFailed(mongers.adminCommand({shardCollection: 'foo', key: "aaa"}));

    // shardCollection may only be run against admin database.
    assert.commandFailed(
        mongers.getDB('test').runCommand({shardCollection: kDbName + '.foo', key: {_id: 1}}));

    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: 1, b: 1}));
    // Can't shard if key is not specified.
    assert.commandFailed(mongers.adminCommand({shardCollection: kDbName + '.foo'}));

    assert.commandFailed(mongers.adminCommand({shardCollection: kDbName + '.foo', key: {}}));

    // Verify key format
    assert.commandFailed(
        mongers.adminCommand({shardCollection: kDbName + '.foo', key: {aKey: "hahahashed"}}));

    // Shard key cannot contain embedded objects.
    assert.commandFailed(
        mongers.adminCommand({shardCollection: kDbName + '.foo', key: {_id: {a: 1}}}));
    assert.commandFailed(
        mongers.adminCommand({shardCollection: kDbName + '.foo', key: {_id: {'a.b': 1}}}));

    // Shard key can contain dotted path to embedded element.
    assert.commandWorked(mongers.adminCommand(
        {shardCollection: kDbName + '.shard_key_dotted_path', key: {'_id.a': 1}}));

    //
    // Test shardCollection's idempotency
    //

    // Succeed if a collection is already sharded with the same options.
    assert.commandWorked(mongers.adminCommand({shardCollection: kDbName + '.foo', key: {_id: 1}}));
    assert.commandWorked(mongers.adminCommand({shardCollection: kDbName + '.foo', key: {_id: 1}}));
    // Specifying the simple collation or not specifying a collation should be equivalent, because
    // if no collation is specified, the collection default collation is used.
    assert.commandWorked(mongers.adminCommand(
        {shardCollection: kDbName + '.foo', key: {_id: 1}, collation: {locale: 'simple'}}));

    // Fail if the collection is already sharded with different options.
    // different shard key
    assert.commandFailed(mongers.adminCommand({shardCollection: kDbName + '.foo', key: {x: 1}}));
    // different 'unique'
    assert.commandFailed(
        mongers.adminCommand({shardCollection: kDbName + '.foo', key: {_id: 1}, unique: true}));

    assert.commandWorked(mongers.getDB(kDbName).dropDatabase());

    // Shard empty collections no index required.
    testAndClenaupWithKeyNoIndexOK({_id: 1});
    testAndClenaupWithKeyNoIndexOK({_id: 'hashed'});

    // Shard by a plain key.
    testAndClenaupWithKeyNoIndexOK({a: 1});

    // Cant shard collection with data and no index on the shard key.
    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: 1, b: 1}));
    testAndClenaupWithKeyNoIndexFailed({a: 1});

    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: 1, b: 1}));
    testAndClenaupWithKeyOK({a: 1});

    // Shard by a hashed key.
    testAndClenaupWithKeyNoIndexOK({a: 'hashed'});

    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: 1, b: 1}));
    testAndClenaupWithKeyNoIndexFailed({a: 'hashed'});

    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: 1, b: 1}));
    testAndClenaupWithKeyOK({a: 'hashed'});

    // Shard by a compound key.
    testAndClenaupWithKeyNoIndexOK({x: 1, y: 1});

    assert.writeOK(mongers.getDB(kDbName).foo.insert({x: 1, y: 1}));
    testAndClenaupWithKeyNoIndexFailed({x: 1, y: 1});

    assert.writeOK(mongers.getDB(kDbName).foo.insert({x: 1, y: 1}));
    testAndClenaupWithKeyOK({x: 1, y: 1});

    testAndClenaupWithKeyNoIndexFailed({x: 'hashed', y: 1});
    testAndClenaupWithKeyNoIndexFailed({x: 'hashed', y: 'hashed'});

    // Shard by a key component.
    testAndClenaupWithKeyOK({'z.x': 1});
    testAndClenaupWithKeyOK({'z.x': 'hashed'});

    // Can't shard by a multikey.
    assert.commandWorked(mongers.getDB(kDbName).foo.createIndex({a: 1}));
    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: [1, 2, 3, 4, 5], b: 1}));
    testAndClenaupWithKeyNoIndexFailed({a: 1});

    assert.commandWorked(mongers.getDB(kDbName).foo.createIndex({a: 1, b: 1}));
    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: [1, 2, 3, 4, 5], b: 1}));
    testAndClenaupWithKeyNoIndexFailed({a: 1, b: 1});

    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: 1, b: 1}));
    testAndClenaupWithKeyNoIndexFailed({a: 'hashed'});

    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: 1, b: 1}));
    testAndClenaupWithKeyOK({a: 'hashed'});

    // Cant shard by a parallel arrays.
    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: [1, 2, 3, 4, 5], b: [1, 2, 3, 4, 5]}));
    testAndClenaupWithKeyNoIndexFailed({a: 1, b: 1});

    assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));

    // Can't shard on unique hashed key.
    assert.commandFailed(mongers.adminCommand(
        {shardCollection: kDbName + '.foo', key: {aKey: "hashed"}, unique: true}));

    // If shardCollection has unique:true it  must have a unique index.
    assert.commandWorked(mongers.getDB(kDbName).foo.createIndex({aKey: 1}));

    assert.commandFailed(
        mongers.adminCommand({shardCollection: kDbName + '.foo', key: {aKey: 1}, unique: true}));

    //
    // Session-related tests
    //

    assert.commandWorked(mongers.getDB(kDbName).dropDatabase());
    assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));

    // shardCollection can be called under a session.
    const sessionDb = mongers.startSession().getDatabase(kDbName);
    assert.commandWorked(
        sessionDb.adminCommand({shardCollection: kDbName + '.foo', key: {_id: 'hashed'}}));
    sessionDb.getSession().endSession();

    assert.commandWorked(mongers.getDB(kDbName).dropDatabase());

    //
    // Collation-related tests
    //

    assert.commandWorked(mongers.getDB(kDbName).dropDatabase());
    assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));

    // shardCollection should fail when the 'collation' option is not a nested object.
    assert.commandFailed(
        mongers.adminCommand({shardCollection: kDbName + '.foo', key: {_id: 1}, collation: true}));

    // shardCollection should fail when the 'collation' option cannot be parsed.
    assert.commandFailed(mongers.adminCommand(
        {shardCollection: kDbName + '.foo', key: {_id: 1}, collation: {locale: 'unknown'}}));

    // shardCollection should fail when the 'collation' option is valid but is not the simple
    // collation.
    assert.commandFailed(mongers.adminCommand(
        {shardCollection: kDbName + '.foo', key: {_id: 1}, collation: {locale: 'en_US'}}));

    // shardCollection should succeed when the 'collation' option specifies the simple collation.
    assert.commandWorked(mongers.adminCommand(
        {shardCollection: kDbName + '.foo', key: {_id: 1}, collation: {locale: 'simple'}}));

    // shardCollection should fail when it does not specify the 'collation' option but the
    // collection has a non-simple default collation.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(
        mongers.getDB(kDbName).createCollection('foo', {collation: {locale: 'en_US'}}));
    assert.commandFailed(mongers.adminCommand({shardCollection: kDbName + '.foo', key: {a: 1}}));

    // shardCollection should fail for the key pattern {_id: 1} if the collection has a non-simple
    // default collation.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(
        mongers.getDB(kDbName).createCollection('foo', {collation: {locale: 'en_US'}}));
    assert.commandFailed(mongers.adminCommand(
        {shardCollection: kDbName + '.foo', key: {_id: 1}, collation: {locale: 'simple'}}));

    // shardCollection should fail for the key pattern {a: 1} if there is already an index 'a_1',
    // but it has a non-simple collation.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(
        mongers.getDB(kDbName).foo.createIndex({a: 1}, {collation: {locale: 'en_US'}}));
    assert.commandFailed(mongers.adminCommand({shardCollection: kDbName + '.foo', key: {a: 1}}));

    // shardCollection should succeed for the key pattern {a: 1} and collation {locale: 'simple'} if
    // there is no index 'a_1', but there is a non-simple collection default collation.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(
        mongers.getDB(kDbName).createCollection('foo', {collation: {locale: 'en_US'}}));
    assert.commandWorked(mongers.adminCommand(
        {shardCollection: kDbName + '.foo', key: {a: 1}, collation: {locale: 'simple'}}));
    var indexSpec = getIndexSpecByName(mongers.getDB(kDbName).foo, 'a_1');
    assert(!indexSpec.hasOwnProperty('collation'));

    // shardCollection should succeed for the key pattern {a: 1} if there are two indexes on {a: 1}
    // and one has the simple collation.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(mongers.getDB(kDbName).foo.createIndex({a: 1}, {name: "a_1_simple"}));
    assert.commandWorked(mongers.getDB(kDbName).foo.createIndex(
        {a: 1}, {collation: {locale: 'en_US'}, name: "a_1_en_US"}));
    assert.commandWorked(mongers.adminCommand({shardCollection: kDbName + '.foo', key: {a: 1}}));

    // shardCollection should fail on a non-empty collection when the only index available with the
    // shard key as a prefix has a non-simple collation.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(
        mongers.getDB(kDbName).createCollection('foo', {collation: {locale: 'en_US'}}));
    assert.writeOK(mongers.getDB(kDbName).foo.insert({a: 'foo'}));
    // This index will inherit the collection's default collation.
    assert.commandWorked(mongers.getDB(kDbName).foo.createIndex({a: 1}));
    assert.commandFailed(mongers.adminCommand(
        {shardCollection: kDbName + '.foo', key: {a: 1}, collation: {locale: 'simple'}}));

    // shardCollection should succeed on an empty collection with a non-simple default collation.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(
        mongers.getDB(kDbName).createCollection('foo', {collation: {locale: 'en_US'}}));
    assert.commandWorked(mongers.adminCommand(
        {shardCollection: kDbName + '.foo', key: {a: 1}, collation: {locale: 'simple'}}));

    // shardCollection should succeed on an empty collection with no default collation.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(mongers.getDB(kDbName).createCollection('foo'));
    assert.commandWorked(mongers.adminCommand({shardCollection: kDbName + '.foo', key: {a: 1}}));

    assert.commandWorked(mongers.getDB(kDbName).dropDatabase());

    //
    // Tests for the shell helper sh.shardCollection().
    //

    db = mongers.getDB(kDbName);
    assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));

    // shardCollection() propagates the shard key and the correct defaults.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(mongers.getDB(kDbName).createCollection('foo'));
    assert.commandWorked(sh.shardCollection(kDbName + '.foo', {a: 1}));
    indexSpec = getIndexSpecByName(mongers.getDB(kDbName).foo, 'a_1');
    assert(!indexSpec.hasOwnProperty('unique'), tojson(indexSpec));
    assert(!indexSpec.hasOwnProperty('collation'), tojson(indexSpec));

    // shardCollection() propagates the value for 'unique'.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(mongers.getDB(kDbName).createCollection('foo'));
    assert.commandWorked(sh.shardCollection(kDbName + '.foo', {a: 1}, true));
    indexSpec = getIndexSpecByName(mongers.getDB(kDbName).foo, 'a_1');
    assert(indexSpec.hasOwnProperty('unique'), tojson(indexSpec));
    assert.eq(indexSpec.unique, true, tojson(indexSpec));

    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(mongers.getDB(kDbName).createCollection('foo'));
    assert.commandWorked(sh.shardCollection(kDbName + '.foo', {a: 1}, false));
    indexSpec = getIndexSpecByName(mongers.getDB(kDbName).foo, 'a_1');
    assert(!indexSpec.hasOwnProperty('unique'), tojson(indexSpec));

    // shardCollections() 'options' parameter must be an object.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(mongers.getDB(kDbName).createCollection('foo'));
    assert.throws(function() {
        sh.shardCollection(kDbName + '.foo', {a: 1}, false, 'not an object');
    });

    // shardCollection() propagates the value for 'collation'.
    // Currently only the simple collation is supported.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(mongers.getDB(kDbName).createCollection('foo'));
    assert.commandFailed(
        sh.shardCollection(kDbName + '.foo', {a: 1}, false, {collation: {locale: 'en_US'}}));
    assert.commandWorked(
        sh.shardCollection(kDbName + '.foo', {a: 1}, false, {collation: {locale: 'simple'}}));
    indexSpec = getIndexSpecByName(mongers.getDB(kDbName).foo, 'a_1');
    assert(!indexSpec.hasOwnProperty('collation'), tojson(indexSpec));

    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(
        mongers.getDB(kDbName).createCollection('foo', {collation: {locale: 'en_US'}}));
    assert.commandFailed(sh.shardCollection(kDbName + '.foo', {a: 1}));
    assert.commandFailed(
        sh.shardCollection(kDbName + '.foo', {a: 1}, false, {collation: {locale: 'en_US'}}));
    assert.commandWorked(
        sh.shardCollection(kDbName + '.foo', {a: 1}, false, {collation: {locale: 'simple'}}));
    indexSpec = getIndexSpecByName(mongers.getDB(kDbName).foo, 'a_1');
    assert(!indexSpec.hasOwnProperty('collation'), tojson(indexSpec));

    // shardCollection() propagates the value for 'numInitialChunks'.
    mongers.getDB(kDbName).foo.drop();
    assert.commandWorked(mongers.getDB(kDbName).createCollection('foo'));
    assert.commandWorked(
        sh.shardCollection(kDbName + '.foo', {a: "hashed"}, false, {numInitialChunks: 5}));
    st.printShardingStatus();
    var numChunks = st.config.chunks.find({ns: kDbName + '.foo'}).count();
    assert.eq(numChunks, 5, "unexpected number of chunks");

    st.stop();

})();
