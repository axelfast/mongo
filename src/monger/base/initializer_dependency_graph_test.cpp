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

/**
 * Unit tests of the InitializerDependencyGraph type.
 */

#include <algorithm>
#include <string>
#include <vector>

#include "monger/base/init.h"
#include "monger/base/initializer_dependency_graph.h"
#include "monger/unittest/unittest.h"

#define STRIP_PARENS_(...) __VA_ARGS__

#define ADD_INITIALIZER(GRAPH, NAME, FN, PREREQS, DEPS)                     \
    (GRAPH).addInitializer((NAME),                                          \
                           (FN),                                            \
                           DeinitializerFunction(),                         \
                           std::vector<std::string>{STRIP_PARENS_ PREREQS}, \
                           std::vector<std::string>{STRIP_PARENS_ DEPS})

#define ASSERT_ADD_INITIALIZER(GRAPH, NAME, FN, PREREQS, DEPS) \
    ASSERT_EQUALS(Status::OK(), ADD_INITIALIZER(GRAPH, NAME, FN, PREREQS, DEPS))

#define ASSERT_EXACTLY_N_IN_CONTAINER(N, CONTAINER, THING) \
    ASSERT_EQUALS(N, std::count((CONTAINER).begin(), (CONTAINER).end(), (THING)))

#define ASSERT_AT_LEAST_N_IN_CONTAINER(N, CONTAINER, THING) \
    ASSERT_LESS_THAN_OR_EQUALS(N, std::count((CONTAINER).begin(), (CONTAINER).end(), (THING)))

#define ASSERT_EXACTLY_ONE_IN_CONTAINER(CONTAINER, THING) \
    ASSERT_EXACTLY_N_IN_CONTAINER(1, CONTAINER, THING)

namespace monger {
namespace {

Status doNothing(InitializerContext*) {
    return Status::OK();
}

TEST(InitializerDependencyGraphTest, InsertNullFunctionFails) {
    InitializerDependencyGraph graph;
    ASSERT_EQUALS(
        ErrorCodes::BadValue,
        ADD_INITIALIZER(
            graph, "A", InitializerFunction(), MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS));
}

TEST(InitializerDependencyGraphTest, InsertSameNameTwiceFails) {
    InitializerDependencyGraph graph;
    ASSERT_ADD_INITIALIZER(graph, "A", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_EQUALS(
        50999,
        ADD_INITIALIZER(graph, "A", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS).code());
}

TEST(InitializerDependencyGraphTest, TopSortEmptyGraph) {
    InitializerDependencyGraph graph;
    std::vector<std::string> nodeNames;
    ASSERT_EQUALS(Status::OK(), graph.topSort(&nodeNames));
    ASSERT_EQUALS(0U, nodeNames.size());
}

TEST(InitializerDependencyGraphTest, TopSortGraphNoDeps) {
    InitializerDependencyGraph graph;
    std::vector<std::string> nodeNames;
    ASSERT_ADD_INITIALIZER(graph, "A", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "B", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "C", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_EQUALS(Status::OK(), graph.topSort(&nodeNames));
    ASSERT_EQUALS(3U, nodeNames.size());
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "A");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "B");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "C");
}

TEST(InitializerDependencyGraphTest, TopSortWithDiamondPrerequisites) {
    /*
     * This tests top-sorting a simple diamond, specified using prerequisites:
     *
     *     B
     *   /  ^
     *  v    \
     * A      D
     *  ^   /
     *   \ v
     *    C
     */
    InitializerDependencyGraph graph;
    std::vector<std::string> nodeNames;
    ASSERT_ADD_INITIALIZER(graph, "A", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "D", doNothing, ("B", "C"), MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "B", doNothing, ("A"), MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "C", doNothing, ("A"), MONGO_NO_DEPENDENTS);
    ASSERT_EQUALS(Status::OK(), graph.topSort(&nodeNames));
    ASSERT_EQUALS(4U, nodeNames.size());
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "A");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "B");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "C");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "D");
    ASSERT_EQUALS("A", nodeNames.front());
    ASSERT_EQUALS("D", nodeNames.back());
}

TEST(InitializerDependencyGraphTest, TopSortWithDiamondDependents) {
    /*
     * This tests top-sorting a simple diamond, specified using dependents:
     *
     *     B
     *   /  ^
     *  v    \
     * A      D
     *  ^   /
     *   \ v
     *    C
     */
    InitializerDependencyGraph graph;
    std::vector<std::string> nodeNames;
    ASSERT_ADD_INITIALIZER(graph, "A", doNothing, MONGO_NO_PREREQUISITES, ("B", "C"));
    ASSERT_ADD_INITIALIZER(graph, "D", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "B", doNothing, MONGO_NO_PREREQUISITES, ("D"));
    ASSERT_ADD_INITIALIZER(graph, "C", doNothing, MONGO_NO_PREREQUISITES, ("D"));
    ASSERT_EQUALS(Status::OK(), graph.topSort(&nodeNames));
    ASSERT_EQUALS(4U, nodeNames.size());
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "A");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "B");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "C");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "D");
    ASSERT_EQUALS("A", nodeNames.front());
    ASSERT_EQUALS("D", nodeNames.back());
}

TEST(InitializerDependencyGraphTest, TopSortWithDiamondGeneral1) {
    /*
     * This tests top-sorting a simple diamond, where B and C specify all prerequisites and
     * dependents.
     *
     *     B
     *   /  ^
     *  v    \
     * A      D
     *  ^   /
     *   \ v
     *    C
     */
    InitializerDependencyGraph graph;
    std::vector<std::string> nodeNames;
    ASSERT_ADD_INITIALIZER(graph, "A", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "D", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "B", doNothing, ("A"), ("D"));
    ASSERT_ADD_INITIALIZER(graph, "C", doNothing, ("A"), ("D"));
    ASSERT_EQUALS(Status::OK(), graph.topSort(&nodeNames));
    ASSERT_EQUALS(4U, nodeNames.size());
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "A");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "B");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "C");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "D");
    ASSERT_EQUALS("A", nodeNames.front());
    ASSERT_EQUALS("D", nodeNames.back());
}

TEST(InitializerDependencyGraphTest, TopSortWithDiamondGeneral2) {
    /*
     * This tests top-sorting a simple diamond, where A and D specify all prerequisites and
     * dependents.
     *
     *     B
     *   /  ^
     *  v    \
     * A      D
     *  ^   /
     *   \ v
     *    C
     */
    InitializerDependencyGraph graph;
    std::vector<std::string> nodeNames;
    ASSERT_ADD_INITIALIZER(graph, "A", doNothing, MONGO_NO_PREREQUISITES, ("B", "C"));
    ASSERT_ADD_INITIALIZER(graph, "D", doNothing, ("C", "B"), MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "B", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "C", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_EQUALS(Status::OK(), graph.topSort(&nodeNames));
    ASSERT_EQUALS(4U, nodeNames.size());
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "A");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "B");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "C");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "D");
    ASSERT_EQUALS("A", nodeNames.front());
    ASSERT_EQUALS("D", nodeNames.back());
}

TEST(InitializerDependencyGraphTest, TopSortWithDiamondGeneral3) {
    /*
     * This tests top-sorting a simple diamond, where A and D specify all prerequisites and
     * dependents, but so do B and C.
     *
     *     B
     *   /  ^
     *  v    \
     * A      D
     *  ^   /
     *   \ v
     *    C
     */
    InitializerDependencyGraph graph;
    std::vector<std::string> nodeNames;
    ASSERT_ADD_INITIALIZER(graph, "A", doNothing, MONGO_NO_PREREQUISITES, ("B", "C"));
    ASSERT_ADD_INITIALIZER(graph, "D", doNothing, ("C", "B"), MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "B", doNothing, ("A"), ("D"));
    ASSERT_ADD_INITIALIZER(graph, "C", doNothing, ("A"), ("D"));
    ASSERT_EQUALS(Status::OK(), graph.topSort(&nodeNames));
    ASSERT_EQUALS(4U, nodeNames.size());
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "A");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "B");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "C");
    ASSERT_EXACTLY_ONE_IN_CONTAINER(nodeNames, "D");
    ASSERT_EQUALS("A", nodeNames.front());
    ASSERT_EQUALS("D", nodeNames.back());
}

TEST(InitializerDependencyGraphTest, TopSortWithDiamondAndCycle) {
    /*
     * This tests top-sorting a graph with a cycle, which should fail..
     *
     *     B <- E
     *   /  ^   ^
     *  v    \ /
     * A      D
     *  ^   /
     *   \ v
     *    C
     */
    InitializerDependencyGraph graph;
    std::vector<std::string> nodeNames;
    ASSERT_ADD_INITIALIZER(graph, "A", doNothing, MONGO_NO_PREREQUISITES, ("B", "C"));
    ASSERT_ADD_INITIALIZER(graph, "D", doNothing, ("C", "B"), MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "B", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "C", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    ASSERT_ADD_INITIALIZER(graph, "E", doNothing, ("D"), ("B"));
    ASSERT_EQUALS(ErrorCodes::GraphContainsCycle, graph.topSort(&nodeNames));
    ASSERT_EQUALS(4U, nodeNames.size());
    ASSERT_EQUALS(nodeNames.front(), nodeNames.back());
    ASSERT_AT_LEAST_N_IN_CONTAINER(1, nodeNames, "D");
    ASSERT_AT_LEAST_N_IN_CONTAINER(1, nodeNames, "E");
    ASSERT_AT_LEAST_N_IN_CONTAINER(1, nodeNames, "B");
    ASSERT_EXACTLY_N_IN_CONTAINER(0, nodeNames, "A");
    ASSERT_EXACTLY_N_IN_CONTAINER(0, nodeNames, "C");
}

TEST(InitializerDependencyGraphTest, TopSortFailsWhenMissingPrerequisite) {
    /*
     * If a node names a never-declared prerequisite, topSort should fail.
     */
    InitializerDependencyGraph graph;
    std::vector<std::string> nodeNames;
    ASSERT_ADD_INITIALIZER(graph, "B", doNothing, ("A"), MONGO_NO_DEPENDENTS);
    auto status = graph.topSort(&nodeNames);
    ASSERT_EQUALS(ErrorCodes::BadValue, status);
    ASSERT_STRING_CONTAINS(status.reason(), "depends on missing initializer A");
}

TEST(InitializerDependencyGraphTest, TopSortFailsWhenMissingDependent) {
    /*
     * If a node names a never-declared dependent, topSort should fail.
     */
    InitializerDependencyGraph graph;
    std::vector<std::string> nodeNames;
    ASSERT_ADD_INITIALIZER(graph, "A", doNothing, MONGO_NO_PREREQUISITES, ("B"));
    auto status = graph.topSort(&nodeNames);
    ASSERT_EQUALS(ErrorCodes::BadValue, status);
    ASSERT_STRING_CONTAINS(status.reason(), "No implementation provided for initializer B");
}

std::vector<std::vector<std::string>> allPermutations(std::vector<std::string> vec,
                                                      size_t first,
                                                      size_t last) {
    std::vector<std::vector<std::string>> out;
    auto i1 = vec.begin() + first;
    auto i2 = vec.begin() + last;
    std::sort(i1, i2);
    do {
        out.push_back(vec);
    } while (std::next_permutation(i1, i2));
    return out;
}

template <typename Expectations, typename F>
void doUntilAllSeen(const Expectations& expected, F&& f) {
    std::vector<int> seen(expected.size(), 0);
    while (std::find(seen.begin(), seen.end(), 0) != seen.end()) {
        auto found = std::find(expected.begin(), expected.end(), f());
        ASSERT_TRUE(found != expected.end());
        ++seen[found - expected.begin()];
    }
}

TEST(InitializerDependencyGraphTest, TopSortShufflesNodes) {
    /*
     * Make sure all node orderings can appear as outputs.
     */
    InitializerDependencyGraph graph;
    std::vector<std::string> graphNodes;
    for (int i = 0; i < 5; ++i) {
        std::string s = "Node" + std::to_string(i);
        graphNodes.push_back(s);
        ASSERT_ADD_INITIALIZER(graph, s, doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    }
    std::vector<std::string> nodeNames;
    doUntilAllSeen(allPermutations(graphNodes, 0, graphNodes.size()), [&]() -> decltype(auto) {
        nodeNames.clear();
        ASSERT_EQUALS(Status::OK(), graph.topSort(&nodeNames));
        return nodeNames;
    });
}

TEST(InitializerDependencyGraphTest, TopSortShufflesChildren) {
    /*
     * Make sure all child orderings can appear as outputs.
     */
    InitializerDependencyGraph graph;
    std::vector<std::string> graphNodes;
    graphNodes.push_back("Parent");
    ASSERT_ADD_INITIALIZER(graph, "Parent", doNothing, MONGO_NO_PREREQUISITES, MONGO_NO_DEPENDENTS);
    for (int i = 0; i < 5; ++i) {
        std::string s = "Child" + std::to_string(i);
        graphNodes.push_back(s);
        ASSERT_ADD_INITIALIZER(graph, s, doNothing, ("Parent"), MONGO_NO_DEPENDENTS);
    }
    std::vector<std::string> nodeNames;
    // Permute only the children.
    doUntilAllSeen(allPermutations(graphNodes, 1, graphNodes.size()), [&]() -> decltype(auto) {
        nodeNames.clear();
        ASSERT_EQUALS(Status::OK(), graph.topSort(&nodeNames));
        return nodeNames;
    });
}

}  // namespace
}  // namespace monger
