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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kNetwork

#include "monger/platform/basic.h"

#include "monger/client/async_client.h"
#include "monger/client/connection_string.h"
#include "monger/db/client.h"
#include "monger/db/operation_context.h"
#include "monger/db/service_context.h"
#include "monger/stdx/thread.h"
#include "monger/transport/session.h"
#include "monger/transport/transport_layer.h"
#include "monger/transport/transport_layer_asio.h"
#include "monger/unittest/integration_test.h"
#include "monger/unittest/unittest.h"
#include "monger/util/fail_point_service.h"
#include "monger/util/log.h"

#include "asio.hpp"

namespace monger {
namespace {

TEST(TransportLayerASIO, HTTPRequestGetsHTTPError) {
    auto connectionString = unittest::getFixtureConnectionString();
    auto server = connectionString.getServers().front();

    asio::io_context ioContext;
    asio::ip::tcp::resolver resolver(ioContext);
    asio::ip::tcp::socket socket(ioContext);

    log() << "Connecting to " << server;
    auto resolverIt = resolver.resolve(server.host(), std::to_string(server.port()));
    asio::connect(socket, resolverIt);

    log() << "Sending HTTP request";
    std::string httpReq = str::stream() << "GET /\r\n"
                                           "Host: "
                                        << server << "\r\n"
                                                     "User-Agent: MongerDB Integration test\r\n"
                                                     "Accept: */*";
    asio::write(socket, asio::buffer(httpReq.data(), httpReq.size()));

    log() << "Waiting for response";
    std::array<char, 256> httpRespBuf;
    std::error_code ec;
    auto size = asio::read(socket, asio::buffer(httpRespBuf.data(), httpRespBuf.size()), ec);
    StringData httpResp(httpRespBuf.data(), size);

    log() << "Received response: \"" << httpResp << "\"";
    ASSERT_TRUE(httpResp.startsWith("HTTP/1.0 200 OK"));

// Why oh why can't ASIO unify their error codes
#ifdef _WIN32
    ASSERT_EQ(ec, asio::error::connection_reset);
#else
    ASSERT_EQ(ec, asio::error::eof);
#endif
}

// This test forces reads and writes to occur one byte at a time, verifying SERVER-34506 (the
// isJustForContinuation optimization works).
//
// Because of the file size limit, it's only an effective check on debug builds (where the future
// implementation checks the length of the future chain).
TEST(TransportLayerASIO, ShortReadsAndWritesWork) {
    const auto assertOK = [](executor::RemoteCommandResponse reply) {
        ASSERT_OK(reply.status);
        ASSERT(reply.data["ok"]) << reply.data;
    };

    auto connectionString = unittest::getFixtureConnectionString();
    auto server = connectionString.getServers().front();

    auto sc = getGlobalServiceContext();
    auto reactor = sc->getTransportLayer()->getReactor(transport::TransportLayer::kNewReactor);

    stdx::thread thread([&] { reactor->run(); });
    const auto threadGuard = makeGuard([&] {
        reactor->stop();
        thread.join();
    });

    AsyncDBClient::Handle handle =
        AsyncDBClient::connect(server, transport::kGlobalSSLMode, sc, reactor, Milliseconds::max())
            .get();

    handle->initWireVersion(__FILE__, nullptr).get();

    FailPointEnableBlock fp("transportLayerASIOshortOpportunisticReadWrite");

    const executor::RemoteCommandRequest ecr{
        server, "admin", BSON("echo" << std::string(1 << 10, 'x')), BSONObj(), nullptr};

    assertOK(handle->runCommandRequest(ecr).get());

    auto client = sc->makeClient(__FILE__);
    auto opCtx = client->makeOperationContext();

    handle->runCommandRequest(ecr, opCtx->getBaton()).get(opCtx.get());
}

TEST(TransportLayerASIO, asyncConnectTimeoutCleansUpSocket) {
    auto connectionString = unittest::getFixtureConnectionString();
    auto server = connectionString.getServers().front();

    auto sc = getGlobalServiceContext();
    auto reactor = sc->getTransportLayer()->getReactor(transport::TransportLayer::kNewReactor);

    stdx::thread thread([&] { reactor->run(); });

    const auto threadGuard = makeGuard([&] {
        reactor->stop();
        thread.join();
    });

    FailPointEnableBlock fp("transportLayerASIOasyncConnectTimesOut");
    auto client =
        AsyncDBClient::connect(server, transport::kGlobalSSLMode, sc, reactor, Milliseconds{500})
            .getNoThrow();
    ASSERT_EQ(client.getStatus(), ErrorCodes::NetworkTimeout);
}

}  // namespace
}  // namespace monger
