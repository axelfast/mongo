(function() {
    var monger = MongerRunner.runMongerd({storageEngine: "devnull"});

    db = monger.getDB("test");

    res = db.foo.insert({x: 1});
    assert.eq(1, res.nInserted, tojson(res));

    // Skip collection validation during stopMongerd if invalid storage engine.
    TestData.skipCollectionAndIndexValidation = true;

    MongerRunner.stopMongerd(monger);
}());
