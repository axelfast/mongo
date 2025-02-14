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
#include "monger/stdx/unordered_map.h"
#include "monger/transport/session.h"
#include "monger/transport/transport_layer.h"
#include "monger/util/net/ssl_types.h"
#include "monger/util/time_support.h"

namespace monger {
namespace transport {

/**
 * This TransportLayerMock is a noop TransportLayer implementation.
 */
class TransportLayerMock : public TransportLayer {
    TransportLayerMock(const TransportLayerMock&) = delete;
    TransportLayerMock& operator=(const TransportLayerMock&) = delete;

public:
    TransportLayerMock();
    ~TransportLayerMock();

    SessionHandle createSession();
    SessionHandle get(Session::Id id);
    bool owns(Session::Id id);

    StatusWith<SessionHandle> connect(HostAndPort peer,
                                      ConnectSSLMode sslMode,
                                      Milliseconds timeout) override;
    Future<SessionHandle> asyncConnect(HostAndPort peer,
                                       ConnectSSLMode sslMode,
                                       const ReactorHandle& reactor,
                                       Milliseconds timeout) override;

    Status setup() override;
    Status start() override;
    void shutdown() override;
    bool inShutdown() const;


    virtual ReactorHandle getReactor(WhichReactor which) override;

    // Set to a factory function to use your own session type.
    std::function<SessionHandle(TransportLayer*)> createSessionHook;

private:
    friend class MockSession;

    struct Connection {
        bool ended;
        SessionHandle session;
        SSLPeerInfo peerInfo;
    };
    stdx::unordered_map<Session::Id, Connection> _sessions;
    bool _shutdown;
};

}  // namespace transport
}  // namespace monger
