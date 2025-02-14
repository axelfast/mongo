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

#include "monger/base/status.h"
#include "monger/base/string_data.h"
#include "monger/bson/bsonobj.h"
#include "monger/rpc/write_concern_error_detail.h"
#include "monger/s/write_ops/write_error_detail.h"

namespace monger {

// Used for reporting legacy write concern responses
struct LegacyWCResponse {
    std::string shardHost;
    BSONObj gleResponse;
    std::string errToReport;
};

//
// Below exposed for testing only
//

// Helper that acts as an auto-ptr for write and wc errors
struct GLEErrors {
    std::unique_ptr<WriteErrorDetail> writeError;
    std::unique_ptr<WriteConcernErrorDetail> wcError;
};

/**
 * Given a GLE response, extracts a write error and a write concern error for the previous
 * operation.
 *
 * Returns !OK if the GLE itself failed in an unknown way.
 */
Status extractGLEErrors(const BSONObj& gleResponse, GLEErrors* errors);

/**
 * Given a GLE response, strips out all non-write-concern related information
 */
BSONObj stripNonWCInfo(const BSONObj& gleResponse);

}  // namespace monger
