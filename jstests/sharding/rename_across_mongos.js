(function() {

    'use strict';

    var st = new ShardingTest({name: 'rename_across_mongers', shards: 1, mongers: 2});
    var dbName = 'RenameDB';

    st.s0.getDB(dbName).dropDatabase();
    st.s1.getDB(dbName).dropDatabase();

    // Create collection on first mongers and insert a document
    assert.commandWorked(st.s0.getDB(dbName).runCommand({create: 'CollNameBeforeRename'}));
    assert.writeOK(st.s0.getDB(dbName).CollNameBeforeRename.insert({Key: 1, Value: 1}));

    if (st.configRS) {
        // Ensure that the second mongers will see the newly created database metadata when
        // it tries to do the collection rename.
        st.configRS.awaitLastOpCommitted();
    }

    // Rename collection on second mongers and ensure the document is found
    assert.commandWorked(
        st.s1.getDB(dbName).CollNameBeforeRename.renameCollection('CollNameAfterRename'));
    assert.eq([{Key: 1, Value: 1}],
              st.s1.getDB(dbName).CollNameAfterRename.find({}, {_id: false}).toArray());

    st.stop();

})();
