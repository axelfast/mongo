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

#include "monger/rpc/reply_builder_interface.h"

#include <utility>

#include "monger/base/status_with.h"
#include "monger/db/jsobj.h"

namespace monger {
namespace rpc {
namespace {

const char kOKField[] = "ok";
const char kCodeField[] = "code";
const char kCodeNameField[] = "codeName";
const char kErrorField[] = "errmsg";

// Similar to appendCommandStatusNoThrow (duplicating logic here to avoid cyclic library dependency)
BSONObj augmentReplyWithStatus(const Status& status, BSONObj reply) {
    auto okField = reply.getField(kOKField);
    if (!okField.eoo() && okField.trueValue()) {
        return reply;
    }

    BSONObjBuilder bob(std::move(reply));
    if (okField.eoo()) {
        bob.append(kOKField, status.isOK() ? 1.0 : 0.0);
    }
    if (status.isOK()) {
        return bob.obj();
    }

    if (!bob.asTempObj().hasField(kErrorField)) {
        bob.append(kErrorField, status.reason());
    }

    if (!bob.asTempObj().hasField(kCodeField)) {
        bob.append(kCodeField, status.code());
        bob.append(kCodeNameField, ErrorCodes::errorString(status.code()));
    }

    if (auto extraInfo = status.extraInfo()) {
        extraInfo->serialize(&bob);
    }

    return bob.obj();
}

}  // namespace

ReplyBuilderInterface& ReplyBuilderInterface::setCommandReply(StatusWith<BSONObj> commandReply) {
    auto reply = commandReply.isOK() ? std::move(commandReply.getValue()) : BSONObj();
    return setRawCommandReply(augmentReplyWithStatus(commandReply.getStatus(), std::move(reply)));
}

ReplyBuilderInterface& ReplyBuilderInterface::setCommandReply(Status nonOKStatus,
                                                              BSONObj extraErrorInfo) {
    invariant(!nonOKStatus.isOK());
    return setRawCommandReply(augmentReplyWithStatus(nonOKStatus, std::move(extraErrorInfo)));
}

}  // namespace rpc
}  // namespace monger
