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

#include "monger/rpc/factory.h"

#include <memory>

#include "monger/rpc/legacy_reply.h"
#include "monger/rpc/legacy_reply_builder.h"
#include "monger/rpc/legacy_request.h"
#include "monger/rpc/legacy_request_builder.h"
#include "monger/rpc/message.h"
#include "monger/rpc/op_msg_rpc_impls.h"
#include "monger/rpc/protocol.h"
#include "monger/util/assert_util.h"
#include "monger/util/str.h"

namespace monger {
namespace rpc {

Message messageFromOpMsgRequest(Protocol proto, const OpMsgRequest& request) {
    switch (proto) {
        case Protocol::kOpMsg:
            return request.serialize();
        case Protocol::kOpQuery:
            return legacyRequestFromOpMsgRequest(request);
    }
    MONGO_UNREACHABLE;
}

std::unique_ptr<ReplyInterface> makeReply(const Message* unownedMessage) {
    switch (unownedMessage->operation()) {
        case monger::dbMsg:
            return std::make_unique<OpMsgReply>(OpMsg::parseOwned(*unownedMessage));
        case monger::opReply:
            return std::make_unique<LegacyReply>(unownedMessage);
        default:
            uasserted(ErrorCodes::UnsupportedFormat,
                      str::stream() << "Received a reply message with unexpected opcode: "
                                    << unownedMessage->operation());
    }
}

OpMsgRequest opMsgRequestFromAnyProtocol(const Message& unownedMessage) {
    switch (unownedMessage.operation()) {
        case monger::dbMsg:
            return OpMsgRequest::parseOwned(unownedMessage);
        case monger::dbQuery:
            return opMsgRequestFromLegacyRequest(unownedMessage);
        default:
            uasserted(ErrorCodes::UnsupportedFormat,
                      str::stream() << "Received a reply message with unexpected opcode: "
                                    << unownedMessage.operation());
    }
}

std::unique_ptr<ReplyBuilderInterface> makeReplyBuilder(Protocol protocol) {
    switch (protocol) {
        case Protocol::kOpMsg:
            return std::make_unique<OpMsgReplyBuilder>();
        case Protocol::kOpQuery:
            return std::make_unique<LegacyReplyBuilder>();
    }
    MONGO_UNREACHABLE;
}

}  // namespace rpc
}  // namespace monger
