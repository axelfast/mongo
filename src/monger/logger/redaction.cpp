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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kDefault

#include "monger/platform/basic.h"

#include "monger/logger/redaction.h"

#include "monger/base/status.h"
#include "monger/bson/bsonobj.h"
#include "monger/util/assert_util.h"
#include "monger/util/log.h"

namespace monger {

namespace {

constexpr auto kRedactionDefaultMask = "###"_sd;

}  // namespace

std::string redact(const BSONObj& objectToRedact) {
    if (!logger::globalLogDomain()->shouldRedactLogs()) {
        return objectToRedact.toString(false);
    }

    return objectToRedact.toString(true);
}

StringData redact(StringData stringToRedact) {
    if (!logger::globalLogDomain()->shouldRedactLogs()) {
        return stringToRedact;
    }

    // Return the default mask.
    return kRedactionDefaultMask;
}

std::string redact(const Status& statusToRedact) {
    if (!logger::globalLogDomain()->shouldRedactLogs()) {
        return statusToRedact.toString();
    }

    // Construct a status representation without the reason()
    StringBuilder sb;
    sb << statusToRedact.codeString();
    if (!statusToRedact.isOK())
        sb << ": " << kRedactionDefaultMask;
    return sb.str();
}

std::string redact(const DBException& exceptionToRedact) {
    if (!logger::globalLogDomain()->shouldRedactLogs()) {
        return exceptionToRedact.toString();
    }

    // Construct an exception representation with the what()
    std::stringstream ss;
    ss << exceptionToRedact.code() << " " << kRedactionDefaultMask;
    return ss.str();
}

}  // namespace monger
