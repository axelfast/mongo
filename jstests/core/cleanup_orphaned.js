// @tags: [requires_non_retryable_commands]

// Test that cleanupOrphaned cannot be run on stand alone mongerd.
var res = db.adminCommand({cleanupOrphaned: 'unsharded.coll'});
assert(!res.ok, tojson(res));
