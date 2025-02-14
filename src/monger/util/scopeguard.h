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

#include <type_traits>

#include "monger/platform/compiler.h"

namespace monger {

template <typename F>
class[[nodiscard]] ScopeGuard {
public:
    explicit ScopeGuard(const F& f) : _func(f) {}
    explicit ScopeGuard(F && f) : _func(std::move(f)) {}

    // Can only be move-constructed, to support being returned from a function.
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard && o) : _func(std::move(o._func)), _dismissed(o._dismissed) {
        o._dismissed = true;
    }
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

    ~ScopeGuard() {
        if (_dismissed)
            return;
        _func();
    }

    void dismiss() noexcept {
        _dismissed = true;
    }

private:
    F _func;
    bool _dismissed = false;
};

template <typename F>
auto makeGuard(F&& fun) {
    return ScopeGuard<std::decay_t<F>>(std::forward<F>(fun));
}

}  // namespace monger

#define MONGO_SCOPEGUARD_CAT2(s1, s2) s1##s2
#define MONGO_SCOPEGUARD_CAT(s1, s2) MONGO_SCOPEGUARD_CAT2(s1, s2)
#define MONGO_SCOPEGUARD_ANON(str) MONGO_SCOPEGUARD_CAT(str, __LINE__)

#define ON_BLOCK_EXIT(...) auto MONGO_SCOPEGUARD_ANON(onBlockExit) = makeGuard(__VA_ARGS__)
