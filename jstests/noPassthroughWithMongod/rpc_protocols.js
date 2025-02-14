// Regression test for SERVER-21673.

// A user can configure the shell to send commands via OP_QUERY or OP_MSG. This can be done at
// startup using the "--rpcProtocols" command line option, or at runtime using the
// "setClientRPCProtocols" method on the Monger object.
// @tags: [requires_profiling]

var RPC_PROTOCOLS = {OP_QUERY: "opQueryOnly", OP_MSG: "opMsgOnly"};

(function() {
    "use strict";

    db.rpcProtocols.drop();

    var oldProfilingLevel = db.getProfilingLevel();

    assert.commandWorked(db.setProfilingLevel(2));

    function runInShell(rpcProtocol, func) {
        assert(0 == _runMongerProgram("monger",
                                     "--rpcProtocols=" + rpcProtocol,
                                     "--readMode=commands",  // ensure we use the find command.
                                     "--eval",
                                     "(" + func.toString() + ")();",
                                     db.getMonger().host));
    }

    // Test that --rpcProtocols=opQueryOnly forces OP_QUERY commands.
    runInShell(RPC_PROTOCOLS.OP_QUERY, function() {
        assert(db.getMonger().getClientRPCProtocols() === "opQueryOnly");
        db.getSiblingDB("test").rpcProtocols.find().comment("opQueryCommandLine").itcount();
    });
    var profileDoc = db.system.profile.findOne({"command.comment": "opQueryCommandLine"});
    assert(profileDoc !== null);
    assert.eq(profileDoc.protocol, "op_query");

    // Test that --rpcProtocols=opMsgOnly forces OP_MSG commands.
    runInShell(RPC_PROTOCOLS.OP_MSG, function() {
        assert(db.getMonger().getClientRPCProtocols() === "opMsgOnly");
        db.getSiblingDB("test").rpcProtocols.find().comment("opMsgCommandLine").itcount();
    });
    profileDoc = db.system.profile.findOne({"command.comment": "opMsgCommandLine"});
    assert(profileDoc !== null);
    assert.eq(profileDoc.protocol, "op_msg");

    // Test that .setClientRPCProtocols("opQueryOnly") forces OP_QUERY commands. We start the shell
    // in OP_MSG only mode, then switch it to OP_QUERY mode at runtime.
    runInShell(RPC_PROTOCOLS.OP_MSG, function() {
        assert(db.getMonger().getClientRPCProtocols() === "opMsgOnly");
        db.getMonger().setClientRPCProtocols("opQueryOnly");
        assert(db.getMonger().getClientRPCProtocols() === "opQueryOnly");
        db.getSiblingDB("test").rpcProtocols.find().comment("opQueryRuntime").itcount();
    });
    profileDoc = db.system.profile.findOne({"command.comment": "opQueryRuntime"});
    assert(profileDoc !== null);
    assert.eq(profileDoc.protocol, "op_query");

    // Test that .setClientRPCProtocols("opMsgOnly") forces OP_MSG commands. We start the
    // shell in OP_QUERY only mode, then switch it to OP_MSG mode at runtime.
    runInShell(RPC_PROTOCOLS.OP_QUERY, function() {
        assert(db.getMonger().getClientRPCProtocols() === "opQueryOnly");
        db.getMonger().setClientRPCProtocols("opMsgOnly");
        assert(db.getMonger().getClientRPCProtocols() === "opMsgOnly");
        db.getSiblingDB("test").rpcProtocols.find().comment("opMsgRuntime").itcount();
    });
    profileDoc = db.system.profile.findOne({"command.comment": "opMsgRuntime"});
    assert(profileDoc !== null);
    assert.eq(profileDoc.protocol, "op_msg");

    // Reset profiling level.
    assert.commandWorked(db.setProfilingLevel(oldProfilingLevel));
})();
