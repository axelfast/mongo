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

#include "monger/platform/basic.h"

#include "monger/platform/stack_locator.h"

#include "monger/util/assert_util.h"

namespace monger {

boost::optional<std::size_t> StackLocator::available() const {
    if (!begin() || !end())
        return boost::none;

    // Technically, it is undefined behavior to compare or subtract
    // two pointers that do not point into the same
    // aggregate. However, we know that these are both pointers within
    // the same stack, and it seems unlikely that the compiler will
    // see that it can elide the comparison here.

    const auto cbegin = reinterpret_cast<const char*>(begin());
    const auto cthis = reinterpret_cast<const char*>(this);
    const auto cend = reinterpret_cast<const char*>(end());

    // TODO: Assumes that stack grows downward
    invariant(cthis <= cbegin);
    invariant(cthis > cend);

    std::size_t avail = cthis - cend;

    return avail;
}

boost::optional<size_t> StackLocator::size() const {
    if (!begin() || !end())
        return boost::none;

    const auto cbegin = reinterpret_cast<const char*>(begin());
    const auto cend = reinterpret_cast<const char*>(end());

    // TODO: Assumes that stack grows downward
    invariant(cbegin > cend);

    return static_cast<size_t>(cbegin - cend);
}

}  // namespace monger
