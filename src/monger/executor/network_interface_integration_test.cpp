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

#include <algorithm>
#include <exception>
#include <memory>

#include "monger/base/status_with.h"
#include "monger/client/connection_string.h"
#include "monger/db/commands/test_commands_enabled.h"
#include "monger/db/wire_version.h"
#include "monger/executor/network_connection_hook.h"
#include "monger/executor/network_interface_integration_fixture.h"
#include "monger/executor/test_network_connection_hook.h"
#include "monger/rpc/factory.h"
#include "monger/rpc/get_status_from_command_result.h"
#include "monger/rpc/message.h"
#include "monger/stdx/future.h"
#include "monger/unittest/integration_test.h"
#include "monger/unittest/unittest.h"
#include "monger/util/assert_util.h"
#include "monger/util/log.h"
#include "monger/util/scopeguard.h"

namespace monger {
namespace executor {
namespace {

bool pingCommandMissing(const RemoteCommandResponse& result) {
    if (result.isOK()) {
        // On mongers, there is no sleep command, so just check that the command failed with
        // a "Command not found" error code
        ASSERT_EQ(result.data["ok"].Double(), 0.0);
        ASSERT_EQ(result.data["code"].Int(), 59);
        return true;
    }

    return false;
}

TEST_F(NetworkInterfaceIntegrationFixture, Ping) {
    startNet();
    assertCommandOK("admin", BSON("ping" << 1));
}

// Hook that intentionally never finishes
class HangingHook : public executor::NetworkConnectionHook {
    Status validateHost(const HostAndPort&,
                        const BSONObj& request,
                        const RemoteCommandResponse&) final {
        return Status::OK();
    }

    StatusWith<boost::optional<RemoteCommandRequest>> makeRequest(
        const HostAndPort& remoteHost) final {
        return {boost::make_optional(RemoteCommandRequest(remoteHost,
                                                          "admin",
                                                          BSON("sleep" << 1 << "lock"
                                                                       << "none"
                                                                       << "secs"
                                                                       << 100000000),
                                                          BSONObj(),
                                                          nullptr))};
    }

    Status handleReply(const HostAndPort& remoteHost, RemoteCommandResponse&& response) final {
        if (!pingCommandMissing(response)) {
            ASSERT_EQ(ErrorCodes::CallbackCanceled, response.status);
            return response.status;
        }

        return {ErrorCodes::ExceededTimeLimit, "No ping command. Returning pseudo-timeout."};
    }
};


// Test that we time out a command if the connection hook hangs.
TEST_F(NetworkInterfaceIntegrationFixture, HookHangs) {
    startNet(std::make_unique<HangingHook>());

    /**
     *  Since mongers's have no ping command, we effectively skip this test by returning
     *  ExceededTimeLimit above. (That ErrorCode is used heavily in repl and sharding code.)
     *  If we return NetworkInterfaceExceededTimeLimit, it will make the ConnectionPool
     *  attempt to reform the connection, which can lead to an accepted but unfortunate
     *  race between TLConnection::setup and TLTypeFactory::shutdown.
     *  We assert here that the error code we get is in the error class of timeouts,
     *  which covers both NetworkInterfaceExceededTimeLimit and ExceededTimeLimit.
     */
    RemoteCommandRequest request{
        fixture().getServers()[0], "admin", BSON("ping" << 1), BSONObj(), nullptr, Seconds(1)};
    auto res = runCommandSync(request);
    ASSERT(ErrorCodes::isExceededTimeLimitError(res.status.code()));
}

using ResponseStatus = TaskExecutor::ResponseStatus;

BSONObj objConcat(std::initializer_list<BSONObj> objs) {
    BSONObjBuilder bob;

    for (const auto& obj : objs) {
        bob.appendElements(obj);
    }

    return bob.obj();
}

class NetworkInterfaceTest : public NetworkInterfaceIntegrationFixture {
public:
    void assertNumOps(uint64_t canceled, uint64_t timedOut, uint64_t failed, uint64_t succeeded) {
        auto counters = net().getCounters();
        ASSERT_EQ(canceled, counters.canceled);
        ASSERT_EQ(timedOut, counters.timedOut);
        ASSERT_EQ(failed, counters.failed);
        ASSERT_EQ(succeeded, counters.succeeded);
    }

    void setUp() override {
        setTestCommandsEnabled(true);
        startNet(std::make_unique<WaitForIsMasterHook>(this));
    }

    RemoteCommandRequest makeTestCommand(boost::optional<Milliseconds> timeout = boost::none,
                                         BSONObj cmd = BSON("echo" << 1 << "foo"
                                                                   << "bar")) {
        auto cs = fixture();
        return RemoteCommandRequest(cs.getServers().front(),
                                    "admin",
                                    std::move(cmd),
                                    BSONObj(),
                                    nullptr,
                                    timeout ? *timeout : RemoteCommandRequest::kNoTimeout);
    }

    struct IsMasterData {
        BSONObj request;
        RemoteCommandResponse response;
    };
    IsMasterData waitForIsMaster() {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        _isMasterCond.wait(lk, [this] { return _isMasterResult != boost::none; });

        return std::move(*_isMasterResult);
    }

    bool hasIsMaster() {
        stdx::lock_guard<stdx::mutex> lk(_mutex);
        return _isMasterResult != boost::none;
    }

private:
    class WaitForIsMasterHook : public NetworkConnectionHook {
    public:
        explicit WaitForIsMasterHook(NetworkInterfaceTest* parent) : _parent(parent) {}

        Status validateHost(const HostAndPort& host,
                            const BSONObj& request,
                            const RemoteCommandResponse& isMasterReply) override {
            stdx::lock_guard<stdx::mutex> lk(_parent->_mutex);
            _parent->_isMasterResult = IsMasterData{request, isMasterReply};
            _parent->_isMasterCond.notify_all();
            return Status::OK();
        }

        StatusWith<boost::optional<RemoteCommandRequest>> makeRequest(const HostAndPort&) override {
            return {boost::none};
        }

        Status handleReply(const HostAndPort&, RemoteCommandResponse&&) override {
            return Status::OK();
        }

    private:
        NetworkInterfaceTest* _parent;
    };

    stdx::mutex _mutex;
    stdx::condition_variable _isMasterCond;
    boost::optional<IsMasterData> _isMasterResult;
};

TEST_F(NetworkInterfaceTest, CancelMissingOperation) {
    // This is just a sanity check, this action should have no effect.
    net().cancelCommand(makeCallbackHandle());
    assertNumOps(0u, 0u, 0u, 0u);
}

TEST_F(NetworkInterfaceTest, CancelOperation) {
    auto cbh = makeCallbackHandle();

    // Kick off our operation
    FailPointEnableBlock fpb("networkInterfaceDiscardCommandsAfterAcquireConn");

    auto deferred = runCommand(cbh, makeTestCommand());

    waitForIsMaster();

    net().cancelCommand(cbh);

    // Wait for op to complete, assert that it was canceled.
    auto result = deferred.get();
    ASSERT_EQ(ErrorCodes::CallbackCanceled, result.status);
    ASSERT(result.elapsedMillis);
    assertNumOps(1u, 0u, 0u, 0u);
}

TEST_F(NetworkInterfaceTest, ImmediateCancel) {
    auto cbh = makeCallbackHandle();

    // Kick off our operation

    FailPointEnableBlock fpb("networkInterfaceDiscardCommandsBeforeAcquireConn");

    auto deferred = runCommand(cbh, makeTestCommand());

    net().cancelCommand(cbh);

    ASSERT_FALSE(hasIsMaster());

    // Wait for op to complete, assert that it was canceled.
    auto result = deferred.get();
    ASSERT_EQ(ErrorCodes::CallbackCanceled, result.status);
    ASSERT(result.elapsedMillis);
    assertNumOps(1u, 0u, 0u, 0u);
}

TEST_F(NetworkInterfaceTest, LateCancel) {
    auto cbh = makeCallbackHandle();

    auto deferred = runCommand(cbh, makeTestCommand());

    // Wait for op to complete, assert that it was canceled.
    auto result = deferred.get();
    net().cancelCommand(cbh);

    ASSERT(result.isOK());
    ASSERT(result.elapsedMillis);
    assertNumOps(0u, 0u, 0u, 1u);
}

TEST_F(NetworkInterfaceTest, AsyncOpTimeout) {
    // Kick off operation
    auto cb = makeCallbackHandle();
    auto request = makeTestCommand(Milliseconds{1000});
    request.cmdObj = BSON("sleep" << 1 << "lock"
                                  << "none"
                                  << "secs"
                                  << 1000000000);
    auto deferred = runCommand(cb, request);

    waitForIsMaster();

    auto result = deferred.get();

    // mongers doesn't implement the ping command, so ignore the response there, otherwise
    // check that we've timed out.
    if (!pingCommandMissing(result)) {
        ASSERT_EQ(ErrorCodes::NetworkInterfaceExceededTimeLimit, result.status);
        ASSERT(result.elapsedMillis);
        assertNumOps(0u, 1u, 0u, 0u);
    }
}

TEST_F(NetworkInterfaceTest, StartCommand) {
    auto commandRequest = BSON("echo" << 1 << "boop"
                                      << "bop");

    // This opmsg request expect the following reply, which is generated below
    // { echo: { echo: 1, boop: "bop", $db: "admin" }, ok: 1.0 }
    auto expectedCommandReply = [&] {
        BSONObjBuilder echoed;
        echoed.appendElements(commandRequest);
        echoed << "$db"
               << "admin";
        return echoed.obj();
    }();
    auto request = makeTestCommand(boost::none, commandRequest);

    auto deferred = runCommand(makeCallbackHandle(), std::move(request));

    auto res = deferred.get();

    ASSERT(res.elapsedMillis);
    uassertStatusOK(res.status);
    ASSERT_BSONOBJ_EQ(res.data.getObjectField("echo"), expectedCommandReply);
    ASSERT_EQ(res.data.getIntField("ok"), 1);
    assertNumOps(0u, 0u, 0u, 1u);
}

TEST_F(NetworkInterfaceTest, SetAlarm) {
    // set a first alarm, to execute after "expiration"
    Date_t expiration = net().now() + Milliseconds(100);
    auto makeTimerFuture = [&] {
        auto pf = makePromiseFuture<Date_t>();
        return std::make_pair([ this, promise = std::move(pf.promise) ](Status status) mutable {
            if (status.isOK()) {
                promise.emplaceValue(net().now());
            } else {
                promise.setError(status);
            }
        },
                              std::move(pf.future));
    };

    auto futurePair = makeTimerFuture();
    ASSERT_OK(net().setAlarm(makeCallbackHandle(), expiration, std::move(futurePair.first)));

    // assert that it executed after "expiration"
    auto& result = futurePair.second.get();
    ASSERT(result >= expiration);

    expiration = net().now() + Milliseconds(99999999);
    auto futurePair2 = makeTimerFuture();
    ASSERT_OK(net().setAlarm(makeCallbackHandle(), expiration, std::move(futurePair2.first)));

    net().shutdown();
    auto swResult = futurePair2.second.getNoThrow();
    ASSERT_FALSE(swResult.isOK());
}

TEST_F(NetworkInterfaceTest, IsMasterRequestContainsOutgoingWireVersionInternalClientInfo) {
    WireSpec::instance().isInternalClient = true;

    auto deferred = runCommand(makeCallbackHandle(), makeTestCommand());
    auto isMasterHandshake = waitForIsMaster();

    // Verify that the isMaster reply has the expected internalClient data.
    auto internalClientElem = isMasterHandshake.request["internalClient"];
    ASSERT_EQ(internalClientElem.type(), BSONType::Object);
    auto minWireVersionElem = internalClientElem.Obj()["minWireVersion"];
    auto maxWireVersionElem = internalClientElem.Obj()["maxWireVersion"];
    ASSERT_EQ(minWireVersionElem.type(), BSONType::NumberInt);
    ASSERT_EQ(maxWireVersionElem.type(), BSONType::NumberInt);
    ASSERT_EQ(minWireVersionElem.numberInt(), WireSpec::instance().outgoing.minWireVersion);
    ASSERT_EQ(maxWireVersionElem.numberInt(), WireSpec::instance().outgoing.maxWireVersion);

    // Verify that the ping op is counted as a success.
    auto res = deferred.get();
    ASSERT(res.elapsedMillis);
    assertNumOps(0u, 0u, 0u, 1u);
}

TEST_F(NetworkInterfaceTest, IsMasterRequestMissingInternalClientInfoWhenNotInternalClient) {
    WireSpec::instance().isInternalClient = false;

    auto deferred = runCommand(makeCallbackHandle(), makeTestCommand());
    auto isMasterHandshake = waitForIsMaster();

    // Verify that the isMaster reply has the expected internalClient data.
    ASSERT_FALSE(isMasterHandshake.request["internalClient"]);
    // Verify that the ping op is counted as a success.
    auto res = deferred.get();
    ASSERT(res.elapsedMillis);
    assertNumOps(0u, 0u, 0u, 1u);
}

}  // namespace
}  // namespace executor
}  // namespace monger
