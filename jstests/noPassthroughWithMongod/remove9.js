t = db.jstests_remove9;
t.drop();

js =
    "while( 1 ) { for( i = 0; i < 10000; ++i ) { db.jstests_remove9.save( {i:i} ); } db.jstests_remove9.remove( {i: {$gte:0} } ); }";
pid = startMongerProgramNoConnect("monger", "--eval", js, db ? db.getMonger().host : null);

Random.setRandomSeed();
for (var i = 0; i < 10000; ++i) {
    assert.writeOK(t.remove({i: Random.randInt(10000)}));
}

stopMongerProgramByPid(pid);
