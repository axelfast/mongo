/**
 * Tests that the $lookup stage respects the collation when the local and/or foreign collections
 * are sharded.
 *
 * The comparison of string values between the 'localField' and 'foreignField' should use the
 * collation either explicitly set on the aggregation operation, or the collation inherited from the
 * collection the "aggregate" command was performed on.
 */
(function() {
    "use strict";

    load("jstests/aggregation/extras/utils.js");                     // for arrayEq
    load("jstests/noPassthrough/libs/server_parameter_helpers.js");  // For setParameterOnAllHosts.
    load("jstests/libs/discover_topology.js");                       // For findDataBearingNodes.

    function runTests(withDefaultCollationColl, withoutDefaultCollationColl, collation) {
        // Test that the $lookup stage respects the inherited collation.
        let res = withDefaultCollationColl
                      .aggregate([{
                          $lookup: {
                              from: withoutDefaultCollationColl.getName(),
                              localField: "str",
                              foreignField: "str",
                              as: "matched",
                          },
                      }])
                      .toArray();
        assert.eq(1, res.length, tojson(res));

        let expected = [{_id: "lowercase", str: "abc"}, {_id: "uppercase", str: "ABC"}];
        assert(arrayEq(expected, res[0].matched),
               "Expected " + tojson(expected) + " to equal " + tojson(res[0].matched) +
                   " up to ordering");

        res = withDefaultCollationColl
                  .aggregate([{
                      $lookup: {
                          from: withoutDefaultCollationColl.getName(),
                          let : {str1: "$str"},
                          pipeline: [
                              {$match: {$expr: {$eq: ["$str", "$$str1"]}}},
                              {
                                $lookup: {
                                    from: withoutDefaultCollationColl.getName(),
                                    let : {str2: "$str"},
                                    pipeline: [{$match: {$expr: {$eq: ["$str", "$$str1"]}}}],
                                    as: "matched2"
                                }
                              }
                          ],
                          as: "matched1",
                      },
                  }])
                  .toArray();
        assert.eq(1, res.length, tojson(res));

        expected = [
            {
              "_id": "lowercase",
              "str": "abc",
              "matched2": [{"_id": "lowercase", "str": "abc"}, {"_id": "uppercase", "str": "ABC"}]
            },
            {
              "_id": "uppercase",
              "str": "ABC",
              "matched2":
                  [{"_id": "lowercase", "str": "abc"}, {"_id": "uppercase", "str": "ABC"}]
            }
        ];
        assert(arrayEq(expected, res[0].matched1),
               "Expected " + tojson(expected) + " to equal " + tojson(res[0].matched1) +
                   " up to ordering. " + tojson(res));

        // Test that the $lookup stage respects the inherited collation when it optimizes with an
        // $unwind stage.
        res = withDefaultCollationColl
                  .aggregate([
                      {
                        $lookup: {
                            from: withoutDefaultCollationColl.getName(),
                            localField: "str",
                            foreignField: "str",
                            as: "matched",
                        },
                      },
                      {$unwind: "$matched"},
                  ])
                  .toArray();
        assert.eq(2, res.length, tojson(res));

        expected = [
            {_id: "lowercase", str: "abc", matched: {_id: "lowercase", str: "abc"}},
            {_id: "lowercase", str: "abc", matched: {_id: "uppercase", str: "ABC"}}
        ];
        assert(arrayEq(expected, res),
               "Expected " + tojson(expected) + " to equal " + tojson(res) + " up to ordering");

        res = withDefaultCollationColl
                  .aggregate([
                      {
                        $lookup: {
                            from: withoutDefaultCollationColl.getName(),
                            let : {str1: "$str"},
                            pipeline: [
                                {$match: {$expr: {$eq: ["$str", "$$str1"]}}},
                                {
                                  $lookup: {
                                      from: withoutDefaultCollationColl.getName(),
                                      let : {str2: "$str"},
                                      pipeline: [{$match: {$expr: {$eq: ["$str", "$$str1"]}}}],
                                      as: "matched2"
                                  }
                                },
                                {$unwind: "$matched2"},
                            ],
                            as: "matched1",
                        },
                      },
                      {$unwind: "$matched1"},
                  ])
                  .toArray();
        assert.eq(4, res.length, tojson(res));

        expected = [
            {
              "_id": "lowercase",
              "str": "abc",
              "matched1": {
                  "_id": "lowercase",
                  "str": "abc",
                  "matched2": {"_id": "lowercase", "str": "abc"}
              }
            },
            {
              "_id": "lowercase",
              "str": "abc",
              "matched1": {
                  "_id": "lowercase",
                  "str": "abc",
                  "matched2": {"_id": "uppercase", "str": "ABC"}
              }
            },
            {
              "_id": "lowercase",
              "str": "abc",
              "matched1": {
                  "_id": "uppercase",
                  "str": "ABC",
                  "matched2": {"_id": "lowercase", "str": "abc"}
              }
            },
            {
              "_id": "lowercase",
              "str": "abc",
              "matched1": {
                  "_id": "uppercase",
                  "str": "ABC",
                  "matched2": {"_id": "uppercase", "str": "ABC"}
              }
            }
        ];
        assert(arrayEq(expected, res),
               "Expected " + tojson(expected) + " to equal " + tojson(res) + " up to ordering");

        // Test that the $lookup stage respects an explicit collation on the aggregation operation.
        res = withoutDefaultCollationColl
                  .aggregate(
                      [
                        {$match: {_id: "lowercase"}},
                        {
                          $lookup: {
                              from: withoutDefaultCollationColl.getName(),
                              localField: "str",
                              foreignField: "str",
                              as: "matched",
                          },
                        },
                      ],
                      collation)
                  .toArray();
        assert.eq(1, res.length, tojson(res));

        expected = [{_id: "lowercase", str: "abc"}, {_id: "uppercase", str: "ABC"}];
        assert(arrayEq(expected, res[0].matched),
               "Expected " + tojson(expected) + " to equal " + tojson(res[0].matched) +
                   " up to ordering");

        res = withoutDefaultCollationColl
                  .aggregate(
                      [
                        {$match: {_id: "lowercase"}},
                        {
                          $lookup: {
                              from: withoutDefaultCollationColl.getName(),
                              let : {str1: "$str"},
                              pipeline: [
                                  {$match: {$expr: {$eq: ["$str", "$$str1"]}}},
                                  {
                                    $lookup: {
                                        from: withoutDefaultCollationColl.getName(),
                                        let : {str2: "$str"},
                                        pipeline: [{$match: {$expr: {$eq: ["$str", "$$str1"]}}}],
                                        as: "matched2"
                                    }
                                  }
                              ],
                              as: "matched1",
                          },
                        }
                      ],
                      collation)
                  .toArray();
        assert.eq(1, res.length, tojson(res));

        expected = [
            {
              "_id": "lowercase",
              "str": "abc",
              "matched2": [{"_id": "lowercase", "str": "abc"}, {"_id": "uppercase", "str": "ABC"}]
            },
            {
              "_id": "uppercase",
              "str": "ABC",
              "matched2":
                  [{"_id": "lowercase", "str": "abc"}, {"_id": "uppercase", "str": "ABC"}]
            }
        ];
        assert(arrayEq(expected, res[0].matched1),
               "Expected " + tojson(expected) + " to equal " + tojson(res[0].matched1) +
                   " up to ordering");

        // Test that the $lookup stage respects an explicit collation on the aggregation operation
        // when
        // it optimizes with an $unwind stage.
        res = withoutDefaultCollationColl
                  .aggregate(
                      [
                        {$match: {_id: "lowercase"}},
                        {
                          $lookup: {
                              from: withoutDefaultCollationColl.getName(),
                              localField: "str",
                              foreignField: "str",
                              as: "matched",
                          },
                        },
                        {$unwind: "$matched"},
                      ],
                      collation)
                  .toArray();
        assert.eq(2, res.length, tojson(res));

        expected = [
            {_id: "lowercase", str: "abc", matched: {_id: "lowercase", str: "abc"}},
            {_id: "lowercase", str: "abc", matched: {_id: "uppercase", str: "ABC"}}
        ];
        assert(arrayEq(expected, res),
               "Expected " + tojson(expected) + " to equal " + tojson(res) + " up to ordering");

        res = withoutDefaultCollationColl
                  .aggregate(
                      [
                        {$match: {_id: "lowercase"}},
                        {
                          $lookup: {
                              from: withoutDefaultCollationColl.getName(),
                              let : {str1: "$str"},
                              pipeline: [
                                  {$match: {$expr: {$eq: ["$str", "$$str1"]}}},
                                  {
                                    $lookup: {
                                        from: withoutDefaultCollationColl.getName(),
                                        let : {str2: "$str"},
                                        pipeline: [{$match: {$expr: {$eq: ["$str", "$$str1"]}}}],
                                        as: "matched2"
                                    }
                                  },
                                  {$unwind: "$matched2"},
                              ],
                              as: "matched1",
                          },
                        },
                        {$unwind: "$matched1"},
                      ],
                      collation)
                  .toArray();
        assert.eq(4, res.length, tojson(res));

        expected = [
            {
              "_id": "lowercase",
              "str": "abc",
              "matched1": {
                  "_id": "lowercase",
                  "str": "abc",
                  "matched2": {"_id": "lowercase", "str": "abc"}
              }
            },
            {
              "_id": "lowercase",
              "str": "abc",
              "matched1": {
                  "_id": "lowercase",
                  "str": "abc",
                  "matched2": {"_id": "uppercase", "str": "ABC"}
              }
            },
            {
              "_id": "lowercase",
              "str": "abc",
              "matched1": {
                  "_id": "uppercase",
                  "str": "ABC",
                  "matched2": {"_id": "lowercase", "str": "abc"}
              }
            },
            {
              "_id": "lowercase",
              "str": "abc",
              "matched1": {
                  "_id": "uppercase",
                  "str": "ABC",
                  "matched2": {"_id": "uppercase", "str": "ABC"}
              }
            }
        ];
        assert(arrayEq(expected, res),
               "Expected " + tojson(expected) + " to equal " + tojson(res) + " up to ordering");

        // Test that the $lookup stage uses the "simple" collation if a collation isn't set on the
        // collection or the aggregation operation.
        res = withoutDefaultCollationColl
                  .aggregate([
                      {$match: {_id: "lowercase"}},
                      {
                        $lookup: {
                            from: withDefaultCollationColl.getName(),
                            localField: "str",
                            foreignField: "str",
                            as: "matched",
                        },
                      },
                  ])
                  .toArray();
        assert.eq([{_id: "lowercase", str: "abc", matched: [{_id: "lowercase", str: "abc"}]}], res);

        res = withoutDefaultCollationColl
                  .aggregate([
                      {$match: {_id: "lowercase"}},
                      {
                        $lookup: {
                            from: withoutDefaultCollationColl.getName(),
                            let : {str1: "$str"},
                            pipeline: [
                                {$match: {$expr: {$eq: ["$str", "$$str1"]}}},
                                {
                                  $lookup: {
                                      from: withoutDefaultCollationColl.getName(),
                                      let : {str2: "$str"},
                                      pipeline: [{$match: {$expr: {$eq: ["$str", "$$str1"]}}}],
                                      as: "matched2"
                                  }
                                },
                                {$unwind: "$matched2"},
                            ],
                            as: "matched1",
                        },
                      },
                  ])
                  .toArray();
        assert.eq([{
                     "_id": "lowercase",
                     "str": "abc",
                     "matched1": [{
                         "_id": "lowercase",
                         "str": "abc",
                         "matched2": {"_id": "lowercase", "str": "abc"}
                     }]
                  }],
                  res);
    }

    const st = new ShardingTest({shards: 2, config: 1});
    setParameterOnAllHosts(
        DiscoverTopology.findNonConfigNodes(st.s), "internalQueryAllowShardedLookup", true);

    const testName = "collation_lookup";
    const caseInsensitive = {collation: {locale: "en_US", strength: 2}};

    const mongersDB = st.s0.getDB(testName);
    const withDefaultCollationColl = mongersDB[testName + "_with_default"];
    const withoutDefaultCollationColl = mongersDB[testName + "_without_default"];

    assert.commandWorked(
        mongersDB.createCollection(withDefaultCollationColl.getName(), caseInsensitive));
    assert.writeOK(withDefaultCollationColl.insert({_id: "lowercase", str: "abc"}));

    assert.writeOK(withoutDefaultCollationColl.insert({_id: "lowercase", str: "abc"}));
    assert.writeOK(withoutDefaultCollationColl.insert({_id: "uppercase", str: "ABC"}));
    assert.writeOK(withoutDefaultCollationColl.insert({_id: "unmatched", str: "def"}));

    //
    // Sharded collection with default collation and unsharded collection without a default
    // collation.
    //
    assert.commandWorked(
        withDefaultCollationColl.createIndex({str: 1}, {collation: {locale: "simple"}}));

    // Enable sharding on the test DB and ensure its primary is shard0000.
    assert.commandWorked(mongersDB.adminCommand({enableSharding: mongersDB.getName()}));
    st.ensurePrimaryShard(mongersDB.getName(), st.shard0.shardName);

    // Shard the collection with a default collation.
    assert.commandWorked(mongersDB.adminCommand({
        shardCollection: withDefaultCollationColl.getFullName(),
        key: {str: 1},
        collation: {locale: "simple"}
    }));

    // Split the collection into 2 chunks.
    assert.commandWorked(mongersDB.adminCommand(
        {split: withDefaultCollationColl.getFullName(), middle: {str: "abc"}}));

    // Move the chunk containing {str: "abc"} to shard0001.
    assert.commandWorked(mongersDB.adminCommand({
        moveChunk: withDefaultCollationColl.getFullName(),
        find: {str: "abc"},
        to: st.shard1.shardName
    }));

    runTests(withDefaultCollationColl, withoutDefaultCollationColl, caseInsensitive);

    // TODO: Enable the following tests once SERVER-32536 is fixed.
    //
    // Sharded collection with default collation and sharded collection without a default
    // collation.
    //

    // Shard the collection without a default collation.
    // assert.commandWorked(mongersDB.adminCommand({
    //     shardCollection: withoutDefaultCollationColl.getFullName(),
    //     key: {_id: 1},
    // }));

    // // Split the collection into 2 chunks.
    // assert.commandWorked(mongersDB.adminCommand(
    //     {split: withoutDefaultCollationColl.getFullName(), middle: {_id: "unmatched"}}));

    // // Move the chunk containing {_id: "lowercase"} to shard0001.
    // assert.commandWorked(mongersDB.adminCommand({
    //     moveChunk: withoutDefaultCollationColl.getFullName(),
    //     find: {_id: "lowercase"},
    //     to: st.shard1.shardName
    // }));

    // runTests(withDefaultCollationColl, withoutDefaultCollationColl, caseInsensitive);

    st.stop();
})();
