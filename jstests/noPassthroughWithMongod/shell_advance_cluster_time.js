/**
 * Santity check getClusterTime and advanceClusterTime.
 */

(function() {
    assert.throws(function() {
        db.getMonger().advanceClusterTime();
    });

    assert.throws(function() {
        db.getMonger().advanceClusterTime(123);
    });

    assert.throws(function() {
        db.getMonger().advanceClusterTime('abc');
    });

    db.getMonger().advanceClusterTime({'clusterTime': 123});

    assert.eq({'clusterTime': 123}, db.getMonger().getClusterTime());

    db.getMonger().advanceClusterTime({'clusterTime': 100});

    assert.eq({'clusterTime': 123}, db.getMonger().getClusterTime());

    db.getMonger().advanceClusterTime({'clusterTime': 456});

    assert.eq({'clusterTime': 456}, db.getMonger().getClusterTime());
})();
