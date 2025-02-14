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

#include <functional>

#include "monger/base/status.h"

namespace monger {
namespace repl {

/**
 * Used by cloner test fixture to centralize life cycle testing.
 *
 * Life cycle interface for collection and database cloners.
 */
class BaseCloner {
public:
    /**
     * Callback function to report final status of cloning.
     */
    using CallbackFn = std::function<void(const Status&)>;

    virtual ~BaseCloner() {}

    /**
     * Returns true if the cloner has been started (but has not completed).
     */
    virtual bool isActive() const = 0;

    /**
     * Starts cloning by scheduling initial command to be run by the executor.
     */
    virtual Status startup() noexcept = 0;

    /**
     * Cancels current remote command request.
     * Returns immediately if cloner is not active.
     *
     * Callback function may be invoked with an ErrorCodes::CallbackCanceled status.
     */
    virtual void shutdown() = 0;

    /**
     * Waits for active remote commands and database worker to complete.
     * Returns immediately if cloner is not active.
     */
    virtual void join() = 0;
};

}  // namespace repl
}  // namespace monger
