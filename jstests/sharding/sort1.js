(function() {
    'use strict';

    var s = new ShardingTest({name: "sort1", shards: 2, mongers: 2});

    s.adminCommand({enablesharding: "test"});
    s.ensurePrimaryShard('test', s.shard1.shardName);
    s.adminCommand({shardcollection: "test.data", key: {'sub.num': 1}});

    var db = s.getDB("test");

    const N = 100;

    var forward = [];
    var backward = [];
    for (var i = 0; i < N; i++) {
        db.data.insert({_id: i, sub: {num: i, x: N - i}});
        forward.push(i);
        backward.push((N - 1) - i);
    }

    s.adminCommand({split: "test.data", middle: {'sub.num': 33}});
    s.adminCommand({split: "test.data", middle: {'sub.num': 66}});

    s.adminCommand({
        movechunk: "test.data",
        find: {'sub.num': 50},
        to: s.getOther(s.getPrimaryShard("test")).name,
        _waitForDelete: true
    });

    assert.lte(3, s.config.chunks.find({ns: 'test.data'}).itcount(), "A1");

    var temp = s.config.chunks.find({ns: 'test.data'}).sort({min: 1}).toArray();
    temp.forEach(printjsononeline);

    var z = 0;
    for (; z < temp.length; z++)
        if (temp[z].min["sub.num"] <= 50 && temp[z].max["sub.num"] > 50)
            break;

    assert.eq(temp[z - 1].shard, temp[z + 1].shard, "A2");
    assert.neq(temp[z - 1].shard, temp[z].shard, "A3");

    temp = db.data.find().sort({'sub.num': 1}).toArray();
    assert.eq(N, temp.length, "B1");
    for (i = 0; i < 100; i++) {
        assert.eq(i, temp[i].sub.num, "B2");
    }

    db.data.find().sort({'sub.num': 1}).toArray();
    s.getPrimaryShard("test").getDB("test").data.find().sort({'sub.num': 1}).toArray();

    var a = Date.timeFunc(function() {
        z = db.data.find().sort({'sub.num': 1}).toArray();
    }, 200);
    assert.eq(100, z.length, "C1");

    var b = 1.5 * Date.timeFunc(function() {
        z = s.getPrimaryShard("test").getDB("test").data.find().sort({'sub.num': 1}).toArray();
    }, 200);
    assert.eq(67, z.length, "C2");

    print("a: " + a + " b:" + b + " mongers slow down: " + Math.ceil(100 * ((a - b) / b)) + "%");

    // -- secondary index sorting

    function getSorted(by, dir, proj) {
        var s = {};
        s[by] = dir || 1;
        printjson(s);
        var cur = db.data.find({}, proj || {}).sort(s);
        return terse(cur.map(function(z) {
            return z.sub.num;
        }));
    }

    function terse(a) {
        var s = "";
        for (var i = 0; i < a.length; i++) {
            if (i > 0)
                s += ",";
            s += a[i];
        }
        return s;
    }

    forward = terse(forward);
    backward = terse(backward);

    assert.eq(forward, getSorted("sub.num", 1), "D1");
    assert.eq(backward, getSorted("sub.num", -1), "D2");

    assert.eq(backward, getSorted("sub.x", 1), "D3");
    assert.eq(forward, getSorted("sub.x", -1), "D4");

    assert.eq(backward, getSorted("sub.x", 1, {'sub.num': 1}), "D5");
    assert.eq(forward, getSorted("sub.x", -1, {'sub.num': 1}), "D6");

    assert.eq(backward, getSorted("sub.x", 1, {'sub': 1}), "D7");
    assert.eq(forward, getSorted("sub.x", -1, {'sub': 1}), "D8");

    assert.eq(backward, getSorted("sub.x", 1, {'_id': 0}), "D9");
    assert.eq(forward, getSorted("sub.x", -1, {'_id': 0}), "D10");

    assert.eq(backward, getSorted("sub.x", 1, {'_id': 0, 'sub.num': 1}), "D11");
    assert.eq(forward, getSorted("sub.x", -1, {'_id': 0, 'sub.num': 1}), "D12");

    s.stop();
})();
