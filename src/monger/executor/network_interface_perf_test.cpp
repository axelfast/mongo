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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kASIO

#include "monger/platform/basic.h"

#include <exception>
#include <memory>

#include "monger/base/status_with.h"
#include "monger/bson/bsonmisc.h"
#include "monger/client/connection_string.h"
#include "monger/executor/async_stream_factory.h"
#include "monger/executor/async_stream_interface.h"
#include "monger/executor/async_timer_asio.h"
#include "monger/executor/network_interface_asio.h"
#include "monger/executor/network_interface_asio_test_utils.h"
#include "monger/executor/task_executor.h"
#include "monger/unittest/integration_test.h"
#include "monger/unittest/unittest.h"
#include "monger/util/assert_util.h"
#include "monger/util/log.h"
#include "monger/util/net/hostandport.h"
#include "monger/util/scopeguard.h"
#include "monger/util/timer.h"

namespace monger {
namespace executor {
namespace {

const std::size_t numOperations = 16384;


int timeNetworkTestMillis(std::size_t operations, NetworkInterface* net) {
    net->startup();
    auto guard = makeGuard([&] { net->shutdown(); });

    auto fixture = unittest::getFixtureConnectionString();
    auto server = fixture.getServers()[0];

    std::atomic<int> remainingOps(operations);  // NOLINT
    stdx::mutex mtx;
    stdx::condition_variable cv;
    Timer t;

    // This lambda function is declared here since it is mutually recursive with another callback
    // function
    std::function<void()> func;

    const auto bsonObjPing = BSON("ping" << 1);

    const auto callback = [&](RemoteCommandResponse resp) {
        uassertStatusOK(resp.status);
        if (--remainingOps) {
            return func();
        }
        stdx::unique_lock<stdx::mutex> lk(mtx);
        cv.notify_one();
    };

    func = [&]() {
        RemoteCommandRequest request{
            server, "admin", bsonObjPing, BSONObj(), nullptr, Milliseconds(-1)};
        net->startCommand(makeCallbackHandle(), request, callback).transitional_ignore();
    };

    func();

    stdx::unique_lock<stdx::mutex> lk(mtx);
    cv.wait(lk, [&] { return remainingOps.load() == 0; });

    return t.millis();
}

TEST(NetworkInterfaceASIO, SerialPerf) {
    NetworkInterfaceASIO::Options options{};
    options.streamFactory = std::make_unique<AsyncStreamFactory>();
    options.timerFactory = std::make_unique<AsyncTimerFactoryASIO>();
    NetworkInterfaceASIO netAsio{std::move(options)};

    int duration = timeNetworkTestMillis(numOperations, &netAsio);
    int result = numOperations * 1000 / duration;
    log() << "THROUGHPUT asio ping ops/s: " << result;
}

}  // namespace
}  // namespace executor
}  // namespace monger
