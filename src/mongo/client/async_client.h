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

#include <memory>

#include "monger/client/authenticate.h"
#include "monger/db/service_context.h"
#include "monger/executor/network_connection_hook.h"
#include "monger/executor/remote_command_request.h"
#include "monger/executor/remote_command_response.h"
#include "monger/rpc/protocol.h"
#include "monger/rpc/unique_message.h"
#include "monger/transport/baton.h"
#include "monger/transport/message_compressor_manager.h"
#include "monger/transport/transport_layer.h"
#include "monger/util/future.h"

namespace monger {

class AsyncDBClient : public std::enable_shared_from_this<AsyncDBClient> {
public:
    explicit AsyncDBClient(const HostAndPort& peer,
                           transport::SessionHandle session,
                           ServiceContext* svcCtx)
        : _peer(std::move(peer)), _session(std::move(session)), _svcCtx(svcCtx) {}

    using Handle = std::shared_ptr<AsyncDBClient>;

    static Future<Handle> connect(const HostAndPort& peer,
                                  transport::ConnectSSLMode sslMode,
                                  ServiceContext* const context,
                                  transport::ReactorHandle reactor,
                                  Milliseconds timeout);

    Future<executor::RemoteCommandResponse> runCommandRequest(
        executor::RemoteCommandRequest request, const BatonHandle& baton = nullptr);
    Future<rpc::UniqueReply> runCommand(OpMsgRequest request, const BatonHandle& baton = nullptr);

    Future<void> authenticate(const BSONObj& params);

    Future<void> authenticateInternal(boost::optional<std::string> mechanismHint);

    Future<void> initWireVersion(const std::string& appName,
                                 executor::NetworkConnectionHook* const hook);

    void cancel(const BatonHandle& baton = nullptr);

    bool isStillConnected();

    void end();

    const HostAndPort& remote() const;
    const HostAndPort& local() const;

private:
    Future<Message> _call(Message request, const BatonHandle& baton = nullptr);
    BSONObj _buildIsMasterRequest(const std::string& appName,
                                  executor::NetworkConnectionHook* hook);
    void _parseIsMasterResponse(BSONObj request,
                                const std::unique_ptr<rpc::ReplyInterface>& response);
    auth::RunCommandHook _makeAuthRunCommandHook();

    const HostAndPort _peer;
    transport::SessionHandle _session;
    ServiceContext* const _svcCtx;
    MessageCompressorManager _compressorManager;
    boost::optional<rpc::Protocol> _negotiatedProtocol;
};

}  // namespace monger
