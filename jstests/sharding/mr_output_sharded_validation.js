// Tests that the cluster mapReduce command will prevent outputting to a sharded collection without
// the sharded: true option specified. Also tests that the command will correctly clean up the
// output namespace of the first phase of a mapReduce with sharded input before the final result
// collection is created. This test was designed to reproduce SERVER-36966.
(function() {
    "use strict";

    const st = new ShardingTest({shards: 2, config: 1, verbose: ''});

    const mongersDB = st.s.getDB("test");
    st.shardColl(mongersDB.foo, {_id: 1}, {_id: 0}, {_id: -1});

    assert.commandWorked(mongersDB.foo.insert([{_id: 1}, {_id: 2}]));

    assert.commandWorked(mongersDB.adminCommand(
        {shardCollection: mongersDB.output.getFullName(), key: {_id: "hashed"}}));

    assert.commandWorked(mongersDB.foo.mapReduce(
        function() {
            emit(this._id, 1);
        },
        function(key, values) {
            return Array.sum(values);
        },
        {out: {replace: "output", sharded: true}}));

    // Test that using just a collection name without specifying a merge mode or the 'sharded: true'
    // information will fail if the named collection is sharded.
    const error = assert.throws(() => mongersDB.foo.mapReduce(
                                    function() {
                                        emit(this._id, 1);
                                    },
                                    function(key, values) {
                                        return Array.sum(values);
                                    },
                                    {out: "output"}));
    assert.eq(error.code, 15920);

    for (let name of mongersDB.getCollectionNames()) {
        assert.eq(-1, name.indexOf("tmp.mrs"), name);
    }

    st.stop();
}());
