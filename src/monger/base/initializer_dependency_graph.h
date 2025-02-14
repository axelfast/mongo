/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongerdb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "monger/base/initializer_function.h"
#include "monger/base/status.h"
#include "monger/stdx/unordered_map.h"
#include "monger/stdx/unordered_set.h"

namespace monger {

class InitializerDependencyNode {
    friend class InitializerDependencyGraph;

public:
    bool isInitialized() const {
        return initialized;
    }
    void setInitialized(bool value) {
        initialized = value;
    };

    InitializerFunction const& getInitializerFunction() const {
        return initFn;
    }
    DeinitializerFunction const& getDeinitializerFunction() const {
        return deinitFn;
    }

private:
    InitializerFunction initFn;
    DeinitializerFunction deinitFn;
    stdx::unordered_set<std::string> prerequisites;
    bool initialized{false};
};

/**
 * Representation of a dependency graph of "initialization operations."
 *
 * Each operation has a unique name, a function object implementing the operation's behavior,
 * and a set of prerequisite operations, which may be empty.  A legal graph contains no cycles.
 *
 * Instances of this class are used in two phases.  In the first phase, the graph is constructed
 * by repeated calls to addInitializer().  In the second phase, a user calls the topSort()
 * method to produce an initialization order that respects the dependencies among operations, and
 * then uses the getInitializerFunction() to get the behavior function for each operation, in
 * turn.
 *
 * Concurrency Notes: The user is responsible for synchronization.  Multiple threads may
 * simultaneously call the const functions, getInitializerFunction and topSort, on the same
 * instance of InitializerDependencyGraph.  However, no thread may call addInitializer while any
 * thread is executing those functions or addInitializer on the same instance.
 */
class InitializerDependencyGraph {
    InitializerDependencyGraph(const InitializerDependencyGraph&) = delete;
    InitializerDependencyGraph& operator=(const InitializerDependencyGraph&) = delete;

public:
    InitializerDependencyGraph();
    ~InitializerDependencyGraph();

    /**
     * Add a new initializer node, named "name", to the dependency graph, with the given
     * behavior, "fn", and the given "prerequisites" (input dependencies) and "dependents"
     * (output dependencies).
     *
     * If "!fn" (fn is NULL in function pointer parlance), returns status with code
     * ErrorCodes::badValue.  If "name" is a duplicate of a name already present in the graph,
     * returns "ErrorCodes::duplicateKey".  Otherwise, returns Status::OK() and adds the new node
     * to the graph.  Note that cycles in the dependency graph are not discovered in this phase.
     * Rather, they're discovered by topSort, below.
     */
    Status addInitializer(std::string name,
                          InitializerFunction initFn,
                          DeinitializerFunction deinitFn,
                          std::vector<std::string> prerequisites,
                          std::vector<std::string> dependents);

    /**
     * Given a dependency operation node named "name", return its behavior function.  Returns
     * a value that evaluates to "false" in boolean context, otherwise.
     */
    InitializerDependencyNode* getInitializerNode(const std::string& name);

    /**
     * Construct a topological sort of the dependency graph, and store that order into
     * "sortedNames".  Returns Status::OK() on success.
     *
     * If the graph contains a cycle, returns ErrorCodes::graphContainsCycle, and "sortedNames"
     * is an ordered sequence of nodes involved in a cycle.  In this case, the first and last
     * element of "sortedNames" will be equal.
     *
     * If any node in the graph names a prerequisite that was never added to the graph via
     * addInitializer, this function will return ErrorCodes::badValue.
     *
     * Any other return value indicates an internal error, and should not occur.
     */
    Status topSort(std::vector<std::string>* sortedNames) const;

private:
    typedef stdx::unordered_map<std::string, InitializerDependencyNode> NodeMap;
    typedef NodeMap::value_type Node;

    /**
     * Map of all named nodes.  Nodes named as prerequisites or dependents but not explicitly
     * added via addInitializer will either be absent from this map or be present with
     * NodeData::fn set to a false-ish value.
     */
    NodeMap _nodes;
};

}  // namespace monger
