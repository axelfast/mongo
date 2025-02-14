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

#include "monger/s/request_types/add_shard_to_zone_request_type.h"

#include "monger/bson/bson_field.h"
#include "monger/bson/util/bson_extract.h"
#include "monger/db/write_concern_options.h"

namespace monger {

using std::string;

namespace {

const char kMongersAddShardToZone[] = "addShardToZone";
const char kConfigsvrAddShardToZone[] = "_configsvrAddShardToZone";
const char kZoneName[] = "zone";

}  // unnamed namespace

StatusWith<AddShardToZoneRequest> AddShardToZoneRequest::parseFromMongersCommand(
    const BSONObj& cmdObj) {
    return _parseFromCommand(cmdObj, true);
}

StatusWith<AddShardToZoneRequest> AddShardToZoneRequest::parseFromConfigCommand(
    const BSONObj& cmdObj) {
    return _parseFromCommand(cmdObj, false);
}

void AddShardToZoneRequest::appendAsConfigCommand(BSONObjBuilder* cmdBuilder) {
    cmdBuilder->append(kConfigsvrAddShardToZone, _shardName);
    cmdBuilder->append(kZoneName, _zoneName);
}

AddShardToZoneRequest::AddShardToZoneRequest(string shardName, string zoneName)
    : _shardName(std::move(shardName)), _zoneName(std::move(zoneName)) {}

StatusWith<AddShardToZoneRequest> AddShardToZoneRequest::_parseFromCommand(const BSONObj& cmdObj,
                                                                           bool forMongers) {
    string shardName;
    auto parseShardNameStatus = bsonExtractStringField(
        cmdObj, (forMongers ? kMongersAddShardToZone : kConfigsvrAddShardToZone), &shardName);

    if (!parseShardNameStatus.isOK()) {
        return parseShardNameStatus;
    }

    string zoneName;
    auto parseZoneNameStatus = bsonExtractStringField(cmdObj, kZoneName, &zoneName);

    if (!parseZoneNameStatus.isOK()) {
        return parseZoneNameStatus;
    }

    return AddShardToZoneRequest(std::move(shardName), std::move(zoneName));
}

const string& AddShardToZoneRequest::getShardName() const {
    return _shardName;
}

const string& AddShardToZoneRequest::getZoneName() const {
    return _zoneName;
}

}  // namespace monger
