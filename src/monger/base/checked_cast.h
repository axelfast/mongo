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

#include <memory>

#include "monger/util/assert_util.h"
#include "monger/util/debug_util.h"

namespace monger {

/**
 * Similar to static_cast, but in debug builds uses RTTI to confirm that the cast
 * is legal at runtime.
 */
template <bool>
struct checked_cast_impl;

template <>
struct checked_cast_impl<false> {
    template <typename T, typename U>
    static T cast(const U& u) {
        return static_cast<T>(u);
    }

    template <typename T, typename U>
    static T cast(U& u) {
        return static_cast<T>(u);
    }
};

template <>
struct checked_cast_impl<true> {
    template <typename T, typename U>
    static T cast(U* u) {
        if (!u) {
            return nullptr;
        }
        T t = dynamic_cast<T>(u);
        invariant(t);
        return t;
    }

    template <typename T, typename U>
    static T cast(const U& u) {
        return dynamic_cast<T>(u);
    }

    template <typename T, typename U>
    static T cast(U& u) {
        return dynamic_cast<T>(u);
    }
};

template <typename T, typename U>
T checked_cast(const U& u) {
    return checked_cast_impl<kDebugBuild>::cast<T>(u);
};

template <typename T, typename U>
T checked_cast(U& u) {
    return checked_cast_impl<kDebugBuild>::cast<T>(u);
};

/**
 * Similar to static_pointer_cast, but in debug builds uses RTTI to confirm that the cast
 * is legal at runtime.
 */
template <bool>
struct checked_pointer_cast_impl;

template <>
struct checked_pointer_cast_impl<false> {
    template <typename T, typename U>
    static std::shared_ptr<T> cast(const std::shared_ptr<U>& u) {
        return std::static_pointer_cast<T>(u);
    }
};

template <>
struct checked_pointer_cast_impl<true> {
    template <typename T, typename U>
    static std::shared_ptr<T> cast(const std::shared_ptr<U>& u) {
        if (!u) {
            return nullptr;
        }

        std::shared_ptr<T> t = std::dynamic_pointer_cast<T>(u);
        invariant(t);

        return t;
    }
};

template <typename T, typename U>
std::shared_ptr<T> checked_pointer_cast(const std::shared_ptr<U>& u) {
    return checked_pointer_cast_impl<kDebugBuild>::cast<T>(u);
};

}  // namespace monger
