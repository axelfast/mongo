// test short-circuiting of $and and $or in
// $project stages to a $const boolean
//
// Cannot implicitly shard accessed collections because the explain output from a mongerd when run
// against a sharded collection is wrapped in a "shards" object with keys for each shard.
//
// This test makes assumptions about how the explain output will be formatted, so cannot be
// transformed to be put inside a $facet stage.
// @tags: [do_not_wrap_aggregations_in_facets,assumes_unsharded_collection]

var t = db.jstests_aggregation_server6192;
t.drop();
t.save({x: true});

function assertOptimized(pipeline, v) {
    var explained = t.runCommand("aggregate", {pipeline: pipeline, explain: true});

    printjson({input: pipeline, output: explained});

    assert("stages" in explained);
    assert("$project" in explained.stages[1]);
    var projectStage = explained.stages[1]["$project"];
    assert.eq(projectStage.a["$const"], v, "ensure short-circuiting worked");
}

function assertNotOptimized(pipeline) {
    var explained = t.runCommand("aggregate", {pipeline: pipeline, explain: true});

    printjson({input: pipeline, output: explained});

    assert("stages" in explained);
    assert("$project" in explained.stages[1]);
    var projectStage = explained.stages[1]["$project"];
    assert(!("$const" in projectStage.a), "ensure no short-circuiting");
}

// short-circuiting for $and
assertOptimized([{$project: {a: {$and: [0, '$x']}}}], false);
assertOptimized([{$project: {a: {$and: [0, 1, '$x']}}}], false);
assertOptimized([{$project: {a: {$and: [0, 1, '', '$x']}}}], false);

assertOptimized([{$project: {a: {$and: [1, 0, '$x']}}}], false);
assertOptimized([{$project: {a: {$and: [1, '', 0, '$x']}}}], false);
assertOptimized([{$project: {a: {$and: [1, 1, 0, 1]}}}], false);

// short-circuiting for $or
assertOptimized([{$project: {a: {$or: [1, '$x']}}}], true);
assertOptimized([{$project: {a: {$or: [1, 0, '$x']}}}], true);
assertOptimized([{$project: {a: {$or: [1, '', '$x']}}}], true);

assertOptimized([{$project: {a: {$or: [0, 1, '$x']}}}], true);
assertOptimized([{$project: {a: {$or: ['', 0, 1, '$x']}}}], true);
assertOptimized([{$project: {a: {$or: [0, 0, 0, 1]}}}], true);

// examples that should not short-circuit
assertNotOptimized([{$project: {a: {$and: [1, '$x']}}}]);
assertNotOptimized([{$project: {a: {$or: [0, '$x']}}}]);
assertNotOptimized([{$project: {a: {$and: ['$x', '$x']}}}]);
assertNotOptimized([{$project: {a: {$or: ['$x', '$x']}}}]);
assertNotOptimized([{$project: {a: {$and: ['$x']}}}]);
assertNotOptimized([{$project: {a: {$or: ['$x']}}}]);
