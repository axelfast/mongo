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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kStorage

#include "monger/db/storage/oplog_hack.h"

#include <limits>

#include "monger/bson/bson_validate.h"
#include "monger/bson/timestamp.h"
#include "monger/db/jsobj.h"
#include "monger/db/record_id.h"
#include "monger/util/debug_util.h"

namespace monger {
namespace oploghack {

StatusWith<RecordId> keyForOptime(const Timestamp& opTime) {
    // Make sure secs and inc wouldn't be negative if treated as signed. This ensures that they
    // don't sort differently when put in a RecordId. It also avoids issues with Null/Invalid
    // RecordIds
    if (opTime.getSecs() > uint32_t(std::numeric_limits<int32_t>::max()))
        return StatusWith<RecordId>(ErrorCodes::BadValue, "ts secs too high");

    if (opTime.getInc() > uint32_t(std::numeric_limits<int32_t>::max()))
        return StatusWith<RecordId>(ErrorCodes::BadValue, "ts inc too high");

    const RecordId out = RecordId(opTime.getSecs(), opTime.getInc());
    if (out <= RecordId::min())
        return StatusWith<RecordId>(ErrorCodes::BadValue, "ts too low");
    if (out >= RecordId::max())
        return StatusWith<RecordId>(ErrorCodes::BadValue, "ts too high");

    return StatusWith<RecordId>(out);
}

/**
 * data and len must be the arguments from RecordStore::insert() on an oplog collection.
 */
StatusWith<RecordId> extractKey(const char* data, int len) {
    // Use the latest BSON validation version. Oplog entries are allowed to contain decimal data
    // even if decimal is disabled.
    DEV invariant(validateBSON(data, len, BSONVersion::kLatest).isOK());

    const BSONObj obj(data);
    const BSONElement elem = obj["ts"];
    if (elem.eoo())
        return StatusWith<RecordId>(ErrorCodes::BadValue, "no ts field");
    if (elem.type() != bsonTimestamp)
        return StatusWith<RecordId>(ErrorCodes::BadValue, "ts must be a Timestamp");

    return keyForOptime(elem.timestamp());
}

}  // namespace oploghack
}  // namespace monger
