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

#include "monger/db/write_concern_options.h"

#include "monger/base/status.h"
#include "monger/base/string_data.h"
#include "monger/bson/util/bson_extract.h"
#include "monger/db/field_parser.h"
#include "monger/util/str.h"

namespace monger {

using std::string;

namespace {

/**
 * Controls how much a client cares about writes and serves as initializer for the pre-defined
 * write concern options.
 *
 * Default is NORMAL.
 */
enum WriteConcern { W_NONE = 0, W_NORMAL = 1 };

constexpr StringData kJFieldName = "j"_sd;
constexpr StringData kFSyncFieldName = "fsync"_sd;
constexpr StringData kWFieldName = "w"_sd;
constexpr StringData kWTimeoutFieldName = "wtimeout"_sd;
constexpr StringData kGetLastErrorFieldName = "getLastError"_sd;
constexpr StringData kWOpTimeFieldName = "wOpTime"_sd;
constexpr StringData kWElectionIdFieldName = "wElectionId"_sd;

}  // namespace

constexpr int WriteConcernOptions::kNoTimeout;
constexpr int WriteConcernOptions::kNoWaiting;

constexpr StringData WriteConcernOptions::kWriteConcernField;
const char WriteConcernOptions::kMajority[] = "majority";

const BSONObj WriteConcernOptions::Default = BSONObj();
const BSONObj WriteConcernOptions::Acknowledged(BSON("w" << W_NORMAL));
const BSONObj WriteConcernOptions::Unacknowledged(BSON("w" << W_NONE));
const BSONObj WriteConcernOptions::Majority(BSON("w" << WriteConcernOptions::kMajority));
constexpr Seconds WriteConcernOptions::kWriteConcernTimeoutSystem;
constexpr Seconds WriteConcernOptions::kWriteConcernTimeoutMigration;
constexpr Seconds WriteConcernOptions::kWriteConcernTimeoutSharding;
constexpr Seconds WriteConcernOptions::kWriteConcernTimeoutUserCommand;


WriteConcernOptions::WriteConcernOptions(int numNodes, SyncMode sync, int timeout)
    : WriteConcernOptions(numNodes, sync, Milliseconds(timeout)) {}

WriteConcernOptions::WriteConcernOptions(const std::string& mode, SyncMode sync, int timeout)
    : WriteConcernOptions(mode, sync, Milliseconds(timeout)) {}

WriteConcernOptions::WriteConcernOptions(int numNodes, SyncMode sync, Milliseconds timeout)
    : syncMode(sync), wNumNodes(numNodes), wTimeout(durationCount<Milliseconds>(timeout)) {}

WriteConcernOptions::WriteConcernOptions(const std::string& mode,
                                         SyncMode sync,
                                         Milliseconds timeout)
    : syncMode(sync), wNumNodes(0), wMode(mode), wTimeout(durationCount<Milliseconds>(timeout)) {}

Status WriteConcernOptions::parse(const BSONObj& obj) {
    reset();
    if (obj.isEmpty()) {
        return Status(ErrorCodes::FailedToParse, "write concern object cannot be empty");
    }

    BSONElement jEl;
    BSONElement fsyncEl;
    BSONElement wEl;

    for (auto e : obj) {
        const auto fieldName = e.fieldNameStringData();
        if (fieldName == kJFieldName) {
            jEl = e;
            if (!jEl.isNumber() && jEl.type() != Bool) {
                return Status(ErrorCodes::FailedToParse, "j must be numeric or a boolean value");
            }
        } else if (fieldName == kFSyncFieldName) {
            fsyncEl = e;
            if (!fsyncEl.isNumber() && fsyncEl.type() != Bool) {
                return Status(ErrorCodes::FailedToParse,
                              "fsync must be numeric or a boolean value");
            }
        } else if (fieldName == kWFieldName) {
            wEl = e;
        } else if (fieldName == kWTimeoutFieldName) {
            wTimeout = e.numberInt();
        } else if (fieldName == kWElectionIdFieldName) {
            // Ignore.
        } else if (fieldName == kWOpTimeFieldName) {
            // Ignore.
        } else if (fieldName.equalCaseInsensitive(kGetLastErrorFieldName)) {
            // Ignore GLE field.
        } else {
            return Status(ErrorCodes::FailedToParse,
                          str::stream() << "unrecognized write concern field: " << fieldName);
        }
    }

    const bool j = jEl.trueValue();
    const bool fsync = fsyncEl.trueValue();

    if (j && fsync)
        return Status(ErrorCodes::FailedToParse, "fsync and j options cannot be used together");

    if (j) {
        syncMode = SyncMode::JOURNAL;
    } else if (fsync) {
        syncMode = SyncMode::FSYNC;
    } else if (!jEl.eoo()) {
        syncMode = SyncMode::NONE;
    }

    if (wEl.isNumber()) {
        wNumNodes = wEl.numberInt();
        usedDefaultW = false;
    } else if (wEl.type() == String) {
        wMode = wEl.valuestrsafe();
        usedDefaultW = false;
    } else if (wEl.eoo() || wEl.type() == jstNULL || wEl.type() == Undefined) {
        wNumNodes = 1;
    } else {
        return Status(ErrorCodes::FailedToParse, "w has to be a number or a string");
    }

    return Status::OK();
}

WriteConcernOptions WriteConcernOptions::deserializerForIDL(const BSONObj& obj) {
    WriteConcernOptions writeConcernOptions;
    uassertStatusOK(writeConcernOptions.parse(obj));
    return writeConcernOptions;
}

StatusWith<WriteConcernOptions> WriteConcernOptions::extractWCFromCommand(
    const BSONObj& cmdObj, const WriteConcernOptions& defaultWC) {
    WriteConcernOptions writeConcern = defaultWC;
    writeConcern.usedDefault = true;
    writeConcern.usedDefaultW = true;
    if (writeConcern.wNumNodes == 0 && writeConcern.wMode.empty()) {
        writeConcern.wNumNodes = 1;
    }

    // Return the default write concern if no write concern is provided. We check for the existence
    // of the write concern field up front in order to avoid the expense of constructing an error
    // status in bsonExtractTypedField() below.
    if (!cmdObj.hasField(kWriteConcernField)) {
        return writeConcern;
    }

    BSONElement writeConcernElement;
    Status wcStatus =
        bsonExtractTypedField(cmdObj, kWriteConcernField, Object, &writeConcernElement);
    if (!wcStatus.isOK()) {
        return wcStatus;
    }

    BSONObj writeConcernObj = writeConcernElement.Obj();
    // Empty write concern is interpreted to default.
    if (writeConcernObj.isEmpty()) {
        return writeConcern;
    }

    wcStatus = writeConcern.parse(writeConcernObj);
    writeConcern.usedDefault = false;
    if (!wcStatus.isOK()) {
        return wcStatus;
    }

    return writeConcern;
}

BSONObj WriteConcernOptions::toBSON() const {
    BSONObjBuilder builder;

    if (wMode.empty()) {
        builder.append("w", wNumNodes);
    } else {
        builder.append("w", wMode);
    }

    if (syncMode == SyncMode::FSYNC) {
        builder.append("fsync", true);
    } else if (syncMode == SyncMode::JOURNAL) {
        builder.append("j", true);
    } else if (syncMode == SyncMode::NONE) {
        builder.append("j", false);
    }

    builder.append("wtimeout", wTimeout);

    return builder.obj();
}

bool WriteConcernOptions::shouldWaitForOtherNodes() const {
    return !wMode.empty() || wNumNodes > 1;
}

}  // namespace monger
