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

#include "monger/rpc/legacy_reply_builder.h"

#include <iterator>
#include <memory>

#include "monger/db/dbmessage.h"
#include "monger/db/jsobj.h"
#include "monger/rpc/metadata.h"
#include "monger/rpc/metadata/sharding_metadata.h"
#include "monger/s/stale_exception.h"
#include "monger/util/assert_util.h"
#include "monger/util/str.h"

namespace monger {
namespace rpc {

LegacyReplyBuilder::LegacyReplyBuilder() : LegacyReplyBuilder(Message()) {}

LegacyReplyBuilder::LegacyReplyBuilder(Message&& message) : _message{std::move(message)} {
    _builder.skip(sizeof(QueryResult::Value));
}

LegacyReplyBuilder::~LegacyReplyBuilder() {}

LegacyReplyBuilder& LegacyReplyBuilder::setCommandReply(Status nonOKStatus,
                                                        BSONObj extraErrorInfo) {
    invariant(!_haveCommandReply);
    if (nonOKStatus == ErrorCodes::StaleConfig) {
        _staleConfigError = true;

        // Need to use the special $err format for StaleConfig errors to be backwards
        // compatible.
        BSONObjBuilder err;

        // $err must be the first field in object.
        err.append("$err", nonOKStatus.reason());
        err.append("code", nonOKStatus.code());
        auto const scex = nonOKStatus.extraInfo<StaleConfigInfo>();
        scex->serialize(&err);
        err.appendElements(extraErrorInfo);
        setRawCommandReply(err.done());
    } else {
        // All other errors proceed through the normal path, which also handles state transitions.
        ReplyBuilderInterface::setCommandReply(std::move(nonOKStatus), std::move(extraErrorInfo));
    }
    return *this;
}

LegacyReplyBuilder& LegacyReplyBuilder::setRawCommandReply(const BSONObj& commandReply) {
    invariant(!_haveCommandReply);
    _bodyOffset = _builder.len();
    commandReply.appendSelfToBufBuilder(_builder);
    _haveCommandReply = true;
    return *this;
}

BSONObjBuilder LegacyReplyBuilder::getBodyBuilder() {
    if (!_haveCommandReply) {
        auto bob = BSONObjBuilder(_builder);
        _bodyOffset = bob.offset();
        _haveCommandReply = true;
        return bob;
    }

    invariant(_bodyOffset);
    return BSONObjBuilder(BSONObjBuilder::ResumeBuildingTag{}, _builder, _bodyOffset);
}


Protocol LegacyReplyBuilder::getProtocol() const {
    return rpc::Protocol::kOpQuery;
}

void LegacyReplyBuilder::reserveBytes(const std::size_t bytes) {
    _builder.reserveBytes(bytes);
    _builder.claimReservedBytes(bytes);
}

void LegacyReplyBuilder::reset() {
    // If we are in State::kMetadata, we are already in the 'start' state, so by
    // immediately returning, we save a heap allocation.
    if (!_haveCommandReply) {
        return;
    }
    _builder.reset();
    _builder.skip(sizeof(QueryResult::Value));
    _message.reset();
    _haveCommandReply = false;
    _staleConfigError = false;
    _bodyOffset = 0;
}


Message LegacyReplyBuilder::done() {
    invariant(_haveCommandReply);

    QueryResult::View qr = _builder.buf();

    if (_staleConfigError) {
        // For compatibility with legacy mongers, we need to set this result flag on StaleConfig
        qr.setResultFlags(ResultFlag_ErrSet | ResultFlag_ShardConfigStale);
    } else {
        qr.setResultFlagsToOk();
    }

    qr.msgdata().setLen(_builder.len());
    qr.msgdata().setOperation(opReply);
    qr.setCursorId(0);
    qr.setStartingFrom(0);
    qr.setNReturned(1);

    _message.setData(_builder.release());

    return std::move(_message);
}

}  // namespace rpc
}  // namespace monger
