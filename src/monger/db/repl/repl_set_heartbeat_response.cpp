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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kReplication

#include "monger/platform/basic.h"

#include "monger/db/repl/repl_set_heartbeat_response.h"

#include <string>

#include "monger/base/status.h"
#include "monger/bson/util/bson_extract.h"
#include "monger/db/jsobj.h"
#include "monger/db/repl/bson_extract_optime.h"
#include "monger/db/server_options.h"
#include "monger/rpc/get_status_from_command_result.h"
#include "monger/util/assert_util.h"
#include "monger/util/log.h"
#include "monger/util/str.h"

namespace monger {
namespace repl {
namespace {

const std::string kConfigFieldName = "config";
const std::string kConfigVersionFieldName = "v";
const std::string kElectionTimeFieldName = "electionTime";
const std::string kMemberStateFieldName = "state";
const std::string kOkFieldName = "ok";
const std::string kDurableOpTimeFieldName = "durableOpTime";
const std::string kDurableWallTimeFieldName = "durableWallTime";
const std::string kAppliedOpTimeFieldName = "opTime";
const std::string kAppliedWallTimeFieldName = "wallTime";
const std::string kPrimaryIdFieldName = "primaryId";
const std::string kReplSetFieldName = "set";
const std::string kSyncSourceFieldName = "syncingTo";
const std::string kTermFieldName = "term";
const std::string kTimestampFieldName = "ts";

}  // namespace

void ReplSetHeartbeatResponse::addToBSON(BSONObjBuilder* builder) const {
    builder->append(kOkFieldName, 1.0);
    if (_electionTimeSet) {
        builder->appendDate(kElectionTimeFieldName,
                            Date_t::fromMillisSinceEpoch(_electionTime.asLL()));
    }
    if (_configSet) {
        *builder << kConfigFieldName << _config.toBSON();
    }
    if (_stateSet) {
        builder->appendIntOrLL(kMemberStateFieldName, _state.s);
    }
    if (_configVersion != -1) {
        *builder << kConfigVersionFieldName << _configVersion;
    }
    if (!_setName.empty()) {
        *builder << kReplSetFieldName << _setName;
    }
    if (!_syncingTo.empty()) {
        *builder << kSyncSourceFieldName << _syncingTo.toString();
    }
    if (_term != -1) {
        builder->append(kTermFieldName, _term);
    }
    if (_primaryIdSet) {
        builder->append(kPrimaryIdFieldName, _primaryId);
    }
    if (_durableOpTimeSet) {
        _durableOpTime.append(builder, kDurableOpTimeFieldName);
        builder->appendDate(kDurableWallTimeFieldName, _durableWallTime);
    }
    if (_appliedOpTimeSet) {
        _appliedOpTime.append(builder, kAppliedOpTimeFieldName);
        builder->appendDate(kAppliedWallTimeFieldName, _appliedWallTime);
    }
}

BSONObj ReplSetHeartbeatResponse::toBSON() const {
    BSONObjBuilder builder;
    addToBSON(&builder);
    return builder.obj();
}

Status ReplSetHeartbeatResponse::initialize(const BSONObj& doc,
                                            long long term,
                                            bool requireWallTime) {
    auto status = getStatusFromCommandResult(doc);
    if (!status.isOK()) {
        return status;
    }

    const BSONElement replSetNameElement = doc[kReplSetFieldName];
    if (replSetNameElement.eoo()) {
        _setName.clear();
    } else if (replSetNameElement.type() != String) {
        return Status(ErrorCodes::TypeMismatch,
                      str::stream() << "Expected \"" << kReplSetFieldName
                                    << "\" field in response to replSetHeartbeat to have "
                                       "type String, but found "
                                    << typeName(replSetNameElement.type()));
    } else {
        _setName = replSetNameElement.String();
    }

    const BSONElement electionTimeElement = doc[kElectionTimeFieldName];
    if (electionTimeElement.eoo()) {
        _electionTimeSet = false;
    } else if (electionTimeElement.type() == Date) {
        _electionTimeSet = true;
        _electionTime = Timestamp(electionTimeElement.date());
    } else {
        return Status(ErrorCodes::TypeMismatch,
                      str::stream() << "Expected \"" << kElectionTimeFieldName
                                    << "\" field in response to replSetHeartbeat "
                                       "command to have type Date, but found type "
                                    << typeName(electionTimeElement.type()));
    }

    Status termStatus = bsonExtractIntegerField(doc, kTermFieldName, &_term);
    if (!termStatus.isOK() && termStatus != ErrorCodes::NoSuchKey) {
        return termStatus;
    }

    status = bsonExtractOpTimeField(doc, kDurableOpTimeFieldName, &_durableOpTime);
    if (!status.isOK()) {
        return status;
    }

    BSONElement durableWallTimeElement;
    _durableWallTime = Date_t();
    status = bsonExtractTypedField(
        doc, kDurableWallTimeFieldName, BSONType::Date, &durableWallTimeElement);
    if (!status.isOK() && (status != ErrorCodes::NoSuchKey || requireWallTime)) {
        // We ignore NoSuchKey errors if the FeatureCompatibilityVersion is less than 4.2, since
        // older version nodes may not report wall clock times.
        return status;
    }
    if (status.isOK()) {
        _durableWallTime = durableWallTimeElement.Date();
    }
    _durableOpTimeSet = true;


    // In V1, heartbeats OpTime is type Object and we construct an OpTime out of its nested fields.
    status = bsonExtractOpTimeField(doc, kAppliedOpTimeFieldName, &_appliedOpTime);
    if (!status.isOK()) {
        return status;
    }

    BSONElement appliedWallTimeElement;
    _appliedWallTime = Date_t();
    status = bsonExtractTypedField(
        doc, kAppliedWallTimeFieldName, BSONType::Date, &appliedWallTimeElement);
    if (!status.isOK() && (status != ErrorCodes::NoSuchKey || requireWallTime)) {
        // We ignore NoSuchKey errors if the FeatureCompatibilityVersion is less than 4.2, since
        // older version nodes may not report wall clock times.
        return status;
    }
    if (status.isOK()) {
        _appliedWallTime = appliedWallTimeElement.Date();
    }
    _appliedOpTimeSet = true;

    const BSONElement memberStateElement = doc[kMemberStateFieldName];
    if (memberStateElement.eoo()) {
        _stateSet = false;
    } else if (memberStateElement.type() != NumberInt && memberStateElement.type() != NumberLong) {
        return Status(
            ErrorCodes::TypeMismatch,
            str::stream() << "Expected \"" << kMemberStateFieldName
                          << "\" field in response to replSetHeartbeat "
                             "command to have type NumberInt or NumberLong, but found type "
                          << typeName(memberStateElement.type()));
    } else {
        long long stateInt = memberStateElement.numberLong();
        if (stateInt < 0 || stateInt > MemberState::RS_MAX) {
            return Status(
                ErrorCodes::BadValue,
                str::stream() << "Value for \"" << kMemberStateFieldName
                              << "\" in response to replSetHeartbeat is "
                                 "out of range; legal values are non-negative and no more than "
                              << MemberState::RS_MAX);
        }
        _stateSet = true;
        _state = MemberState(static_cast<int>(stateInt));
    }

    const BSONElement configVersionElement = doc[kConfigVersionFieldName];
    if (configVersionElement.eoo()) {
        return Status(ErrorCodes::NoSuchKey,
                      str::stream() << "Response to replSetHeartbeat missing required \""
                                    << kConfigVersionFieldName
                                    << "\" field");
    }
    if (configVersionElement.type() != NumberInt) {
        return Status(ErrorCodes::TypeMismatch,
                      str::stream() << "Expected \"" << kConfigVersionFieldName
                                    << "\" field in response to replSetHeartbeat to have "
                                       "type NumberInt, but found "
                                    << typeName(configVersionElement.type()));
    }
    _configVersion = configVersionElement.numberInt();

    const BSONElement syncingToElement = doc[kSyncSourceFieldName];
    if (syncingToElement.eoo()) {
        _syncingTo = HostAndPort();
    } else if (syncingToElement.type() != String) {
        return Status(ErrorCodes::TypeMismatch,
                      str::stream() << "Expected \"" << kSyncSourceFieldName
                                    << "\" field in response to replSetHeartbeat to "
                                       "have type String, but found "
                                    << typeName(syncingToElement.type()));
    } else {
        _syncingTo = HostAndPort(syncingToElement.String());
    }

    const BSONElement rsConfigElement = doc[kConfigFieldName];
    if (rsConfigElement.eoo()) {
        _configSet = false;
        _config = ReplSetConfig();
        return Status::OK();
    } else if (rsConfigElement.type() != Object) {
        return Status(ErrorCodes::TypeMismatch,
                      str::stream() << "Expected \"" << kConfigFieldName
                                    << "\" in response to replSetHeartbeat to have type "
                                       "Object, but found "
                                    << typeName(rsConfigElement.type()));
    }
    _configSet = true;

    return _config.initialize(rsConfigElement.Obj());
}

MemberState ReplSetHeartbeatResponse::getState() const {
    invariant(_stateSet);
    return _state;
}

Timestamp ReplSetHeartbeatResponse::getElectionTime() const {
    invariant(_electionTimeSet);
    return _electionTime;
}

const ReplSetConfig& ReplSetHeartbeatResponse::getConfig() const {
    invariant(_configSet);
    return _config;
}

long long ReplSetHeartbeatResponse::getPrimaryId() const {
    invariant(_primaryIdSet);
    return _primaryId;
}

OpTime ReplSetHeartbeatResponse::getAppliedOpTime() const {
    invariant(_appliedOpTimeSet);
    return _appliedOpTime;
}

OpTimeAndWallTime ReplSetHeartbeatResponse::getAppliedOpTimeAndWallTime() const {
    invariant(_appliedOpTimeSet);
    return {_appliedOpTime, _appliedWallTime};
}

OpTime ReplSetHeartbeatResponse::getDurableOpTime() const {
    invariant(_durableOpTimeSet);
    return _durableOpTime;
}

OpTimeAndWallTime ReplSetHeartbeatResponse::getDurableOpTimeAndWallTime() const {
    invariant(_durableOpTimeSet);
    return {_durableOpTime, _durableWallTime};
}

}  // namespace repl
}  // namespace monger
