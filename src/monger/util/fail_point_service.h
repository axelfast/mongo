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

#include "monger/base/init.h"
#include "monger/util/fail_point.h"
#include "monger/util/fail_point_registry.h"

namespace monger {

/**
 * @return the global fail point registry.
 */
FailPointRegistry* getGlobalFailPointRegistry();

/**
 * Set a fail point in the global registry to a given value via BSON
 * @throw DBException If no failpoint called failPointName exists.
 */
void setGlobalFailPoint(const std::string& failPointName, const BSONObj& cmdObj);

/**
 * Convenience macro for defining a fail point. Must be used at namespace scope.
 * Note: that means never at local scope (inside functions) or class scope.
 * NOTE: Never use in header files, only sources.
 */
#define MONGO_FAIL_POINT_DEFINE(fp)                                                   \
    ::monger::FailPoint fp;                                                            \
    MONGO_INITIALIZER_GENERAL(fp, ("FailPointRegistry"), ("AllFailPointsRegistered")) \
    (::monger::InitializerContext * context) {                                         \
        return ::monger::getGlobalFailPointRegistry()->addFailPoint(#fp, &fp);         \
    }

/**
 * Convenience macro for declaring a fail point in a header.
 */
#define MONGO_FAIL_POINT_DECLARE(fp) extern ::monger::FailPoint fp;

/**
 * Convenience class for enabling a failpoint and disabling it as this goes out of scope.
 */
class FailPointEnableBlock {
public:
    FailPointEnableBlock(const std::string& failPointName);
    FailPointEnableBlock(const std::string& failPointName, const BSONObj& cmdObj);
    ~FailPointEnableBlock();

private:
    FailPoint* _failPoint;
    const std::string _failPointName;
};

}  // namespace monger
