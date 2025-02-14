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

#include <pthread.h>
#include <pthread_np.h>

#include "monger/util/assert_util.h"
#include "monger/util/scopeguard.h"

namespace monger {

StackLocator::StackLocator() {
    pthread_attr_t attr;
    size_t size;

    pthread_t self = pthread_self();

    invariant(pthread_attr_init(&attr) == 0);
    ON_BLOCK_EXIT([&] { pthread_attr_destroy(&attr); });

    invariant(pthread_attr_get_np(self, &attr) == 0);

    invariant(pthread_attr_getstack(&attr, &_end, &size) == 0);

    // TODO: Assumes stack grows downward on FreeBSD
    _begin = static_cast<char*>(_end) + size;
}

}  // namespace monger
