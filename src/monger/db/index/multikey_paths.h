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

#include <cstddef>
#include <set>
#include <vector>

namespace monger {

// If non-empty, a vector with size equal to the number of elements in the index key pattern. Each
// element in the vector is an ordered set of positions (starting at 0) into the corresponding
// indexed field that represent what prefixes of the indexed field cause the index to be multikey.
//
// For example, with the index {'a.b': 1, 'a.c': 1} where the paths "a" and "a.b" cause the
// index to be multikey, we'd have a std::vector<std::set<size_t>>{{0U, 1U}, {0U}}.
//
// Further Examples:
// Index                  PathsThatAreMultiKey  MultiKeyPaths
// --------------------   --------------------  --------------------
// {'a.b': 1, 'a.c': 1}   "a", "a.b"            {{0U, 1U}, {0U}}
// {a: 1, b: 1}           "b"                   {{}, {0U}}
// {a: 1, b: 1}           "a"                   {{0U}, {}}
// {'a.b.c': 1, d: 1}     "a.b.c"               {{2U}, {}}
// {'a.b': 1, c: 1, d: 1} "a.b", "d"            {{1U}, {}, {0U}}
// {a: 1, b: 1}           none                  {{}, {}}
// {a: 1, b: 1}           no multikey metadata  {}
//
// An empty vector is used to represent that the index doesn't support path-level multikey tracking.
using MultikeyPaths = std::vector<std::set<std::size_t>>;

}  // namespace monger
