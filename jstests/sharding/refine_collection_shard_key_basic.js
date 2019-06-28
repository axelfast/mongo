//
// Basic tests for refineCollectionShardKey.
//

(function() {
    'use strict';

    const st = new ShardingTest({mongers: 1, shards: 2});
    const mongers = st.s;
    const kDbName = 'db';

    assert.commandWorked(mongers.adminCommand({enableSharding: kDbName}));
    assert.commandWorked(
        mongers.adminCommand({refineCollectionShardKey: kDbName + '.foo', key: {aKey: 1}}));

    st.stop();
})();
