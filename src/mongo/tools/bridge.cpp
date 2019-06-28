/**
 *    Copyright (C) 2018-present MongerDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongerDB, Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kBridge

#include "monger/platform/basic.h"

#include <boost/optional.hpp>
#include <cstdint>
#include <memory>

#include "monger/base/init.h"
#include "monger/base/initializer.h"
#include "monger/db/dbmessage.h"
#include "monger/db/operation_context.h"
#include "monger/db/service_context.h"
#include "monger/platform/atomic_word.h"
#include "monger/platform/random.h"
#include "monger/rpc/factory.h"
#include "monger/rpc/message.h"
#include "monger/rpc/reply_builder_interface.h"
#include "monger/stdx/mutex.h"
#include "monger/stdx/thread.h"
#include "monger/tools/bridge_commands.h"
#include "monger/tools/mongerbridge_options.h"
#include "monger/transport/message_compressor_manager.h"
#include "monger/transport/service_entry_point_impl.h"
#include "monger/transport/service_executor_synchronous.h"
#include "monger/transport/transport_layer_asio.h"
#include "monger/util/assert_util.h"
#include "monger/util/exit.h"
#include "monger/util/log.h"
#include "monger/util/quick_exit.h"
#include "monger/util/signal_handlers.h"
#include "monger/util/str.h"
#include "monger/util/text.h"
#include "monger/util/time_support.h"
#include "monger/util/timer.h"

namespace monger {

namespace {

boost::optional<HostAndPort> extractHostInfo(const OpMsgRequest& request) {
    // The initial isMaster request made by mongerd and mongers processes should contain a hostInfo
    // field that identifies the process by its host:port.
    StringData cmdName = request.getCommandName();
    if (cmdName != "isMaster" && cmdName != "ismaster") {
        return boost::none;
    }

    if (auto hostInfoElem = request.body["hostInfo"]) {
        if (hostInfoElem.type() == String) {
            return HostAndPort{hostInfoElem.valueStringData()};
        }
    }
    return boost::none;
}

}  // namespace

class BridgeContext {
public:
    Status runBridgeCommand(StringData cmdName, BSONObj cmdObj) {
        auto status = BridgeCommand::findCommand(cmdName);
        if (!status.isOK()) {
            return status.getStatus();
        }

        log() << "Processing bridge command: " << cmdName;

        BridgeCommand* command = status.getValue();
        return command->run(cmdObj, &_settingsMutex, &_settings);
    }

    boost::optional<Status> maybeProcessBridgeCommand(boost::optional<OpMsgRequest> cmdRequest) {
        if (!cmdRequest) {
            return boost::none;
        }

        if (auto forBridge = cmdRequest->body["$forBridge"]) {
            if (forBridge.trueValue()) {
                return runBridgeCommand(cmdRequest->getCommandName(), cmdRequest->body);
            }
            return boost::none;
        }

        return boost::none;
    }

    HostSettings getHostSettings(boost::optional<HostAndPort> host) {
        if (host) {
            stdx::lock_guard<stdx::mutex> lk(_settingsMutex);
            return (_settings)[*host];
        }
        return {};
    }

    PseudoRandom makeSeededPRNG() {
        static PseudoRandom globalPRNG(mongerBridgeGlobalParams.seed);
        return PseudoRandom(globalPRNG.nextInt64());
    }

    static BridgeContext* get();

private:
    static const ServiceContext::Decoration<BridgeContext> _get;

    stdx::mutex _settingsMutex;
    HostSettingsMap _settings;
};

const ServiceContext::Decoration<BridgeContext> BridgeContext::_get =
    ServiceContext::declareDecoration<BridgeContext>();

BridgeContext* BridgeContext::get() {
    return &_get(getGlobalServiceContext());
}

class ServiceEntryPointBridge;
class ProxiedConnection {
public:
    ProxiedConnection() : _dest(nullptr), _prng(BridgeContext::get()->makeSeededPRNG()) {}

    transport::Session* operator->() {
        return _dest.get();
    }

    transport::SessionHandle& getSession() {
        return _dest;
    }

    void setSession(transport::SessionHandle session) {
        _dest = std::move(session);
    }

    const boost::optional<HostAndPort>& host() const {
        return _host;
    }

    std::string toString() const {
        if (_host) {
            return _host->toString();
        }
        return "<unknown>";
    }

    void setExhaust(bool val) {
        _inExhaust = val;
    }

    bool inExhaust() const {
        return _inExhaust;
    }

    void extractHostInfo(OpMsgRequest request) {
        if (_seenFirstMessage)
            return;
        _seenFirstMessage = true;

        // The initial isMaster request made by mongerd and mongers processes should contain a
        // hostInfo field that identifies the process by its host:port.
        StringData cmdName = request.getCommandName();
        if (cmdName != "isMaster" && cmdName != "ismaster") {
            return;
        }

        if (auto hostInfoElem = request.body["hostInfo"]) {
            if (hostInfoElem.type() == String) {
                _host = HostAndPort{hostInfoElem.valueStringData()};
            }
        }
    }

    double nextCanonicalDouble() {
        return _prng.nextCanonicalDouble();
    }

    static ProxiedConnection& get(const transport::SessionHandle& session);

private:
    friend class ServiceEntryPointBridge;

    static const transport::Session::Decoration<ProxiedConnection> _get;
    transport::SessionHandle _dest;
    PseudoRandom _prng;
    boost::optional<HostAndPort> _host;
    bool _seenFirstMessage = false;
    bool _inExhaust = false;
};

const transport::Session::Decoration<ProxiedConnection> ProxiedConnection::_get =
    transport::Session::declareDecoration<ProxiedConnection>();

ProxiedConnection& ProxiedConnection::get(const transport::SessionHandle& session) {
    return _get(*session);
}

class ServiceEntryPointBridge final : public ServiceEntryPointImpl {
public:
    explicit ServiceEntryPointBridge(ServiceContext* svcCtx) : ServiceEntryPointImpl(svcCtx) {}

    DbResponse handleRequest(OperationContext* opCtx, const Message& request) final;
};

DbResponse ServiceEntryPointBridge::handleRequest(OperationContext* opCtx, const Message& request) {
    const auto& source = opCtx->getClient()->session();
    auto& dest = ProxiedConnection::get(source);
    auto brCtx = BridgeContext::get();

    if (!dest.getSession()) {
        dest.setSession([]() -> transport::SessionHandle {
            HostAndPort destAddr{mongerBridgeGlobalParams.destUri};
            const Seconds kConnectTimeout(30);
            auto now = getGlobalServiceContext()->getFastClockSource()->now();
            const auto connectExpiration = now + kConnectTimeout;
            while (now < connectExpiration) {
                auto tl = getGlobalServiceContext()->getTransportLayer();
                auto sws =
                    tl->connect(destAddr, transport::kGlobalSSLMode, connectExpiration - now);
                auto status = sws.getStatus();
                if (!status.isOK()) {
                    warning() << "Unable to establish connection to " << destAddr << ": " << status;
                    now = getGlobalServiceContext()->getFastClockSource()->now();
                } else {
                    return std::move(sws.getValue());
                }

                sleepmillis(500);
            }

            return nullptr;
        }());

        if (!dest.getSession()) {
            source->end();
            uasserted(50861, str::stream() << "Unable to connect to " << source->remote());
        }
    }

    if (dest.inExhaust()) {
        DbMessage dbm(request);

        auto response = uassertStatusOK(dest->sourceMessage());
        if (response.operation() == dbCompressed) {
            MessageCompressorManager compressorMgr;
            response = uassertStatusOK(compressorMgr.decompressMessage(response));
        }

        MsgData::View header = response.header();
        QueryResult::View qr = header.view2ptr();
        if (qr.getCursorId()) {
            return {std::move(response)};
        } else {
            dest.setExhaust(false);
            return {Message(), dbm.getns()};
        }
    }

    const bool isFireAndForgetCommand = OpMsg::isFlagSet(request, OpMsg::kMoreToCome);

    boost::optional<OpMsgRequest> cmdRequest;
    if ((request.operation() == dbQuery &&
         NamespaceString(DbMessage(request).getns()).isCommand()) ||
        request.operation() == dbMsg) {
        cmdRequest = rpc::opMsgRequestFromAnyProtocol(request);

        dest.extractHostInfo(*cmdRequest);

        LOG(1) << "Received \"" << cmdRequest->getCommandName() << "\" command with arguments "
               << cmdRequest->body << " from " << dest;
    }

    // Handle a message intended to configure the mongerbridge and return a response.
    // The 'request' is consumed by the mongerbridge and does not get forwarded to
    // 'dest'.
    if (auto status = brCtx->maybeProcessBridgeCommand(cmdRequest)) {
        invariant(!isFireAndForgetCommand);

        auto replyBuilder = rpc::makeReplyBuilder(rpc::protocolForMessage(request));
        BSONObj reply;
        StatusWith<BSONObj> commandReply(reply);
        if (!status->isOK()) {
            commandReply = StatusWith<BSONObj>(*status);
        }
        return {replyBuilder->setCommandReply(std::move(commandReply)).done()};
    }


    // Get the message handling settings for 'host' if the source of the connection is
    // known. By default, messages are forwarded to 'dest' without any additional delay.
    HostSettings hostSettings = brCtx->getHostSettings(dest.host());

    switch (hostSettings.state) {
        // Close the connection to 'dest'.
        case HostSettings::State::kHangUp:
            log() << "Rejecting connection from " << dest << ", end connection "
                  << source->remote().toString();
            source->end();
            return {Message()};
        // Forward the message to 'dest' with probability '1 - hostSettings.loss'.
        case HostSettings::State::kDiscard:
            if (dest.nextCanonicalDouble() < hostSettings.loss) {
                std::string hostName = dest.toString();
                if (cmdRequest) {
                    log() << "Discarding \"" << cmdRequest->getCommandName()
                          << "\" command with arguments " << cmdRequest->body << " from "
                          << hostName;
                } else {
                    log() << "Discarding " << networkOpToString(request.operation()) << " from "
                          << hostName;
                }
                return {Message()};
            }
        // Forward the message to 'dest' after waiting for 'hostSettings.delay'
        // milliseconds.
        case HostSettings::State::kForward:
            sleepmillis(durationCount<Milliseconds>(hostSettings.delay));
            break;
    }

    uassertStatusOK(dest->sinkMessage(request));

    // Send the message we received from 'source' to 'dest'. 'dest' returns a response for
    // OP_QUERY, OP_GET_MORE, and OP_MSG messages that we respond with.
    if (!isFireAndForgetCommand &&
        (request.operation() == dbQuery || request.operation() == dbGetMore ||
         request.operation() == dbMsg)) {
        // TODO dbMsg moreToCome
        // Forward the message to 'dest' and receive its reply in 'response'.
        auto response = uassertStatusOK(dest->sourceMessage());
        uassert(50765,
                "Response ID did not match the sent message ID.",
                response.header().getResponseToMsgId() == request.header().getId());

        // Reload the message handling settings for 'host' in case they were changed
        // while waiting for a response from 'dest'.
        hostSettings = brCtx->getHostSettings(dest.host());

        // It's possible that sending 'request' blocked until 'dest' had something to
        // reply with. If the message handling settings were since changed to close
        // connections from 'host', then do so now.
        if (hostSettings.state == HostSettings::State::kHangUp) {
            log() << "Closing connection from " << dest << ", end connection " << source->remote();
            source->end();
            return {Message()};
        }

        std::string exhaustNS;
        if (request.operation() == dbQuery) {
            DbMessage d(request);
            QueryMessage q(d);
            dest.setExhaust(q.queryOptions & QueryOption_Exhaust);
            if (dest.inExhaust()) {
                exhaustNS = d.getns();
            }
        } else {
            dest.setExhaust(false);
        }

        // The original checksum won't be valid once the network layer replaces requestId. Remove it
        // because the network layer re-checksums the response.
        OpMsg::removeChecksum(&response);
        return {std::move(response), exhaustNS};
    } else {
        return {Message()};
    }
}

int bridgeMain(int argc, char** argv, char** envp) {

    registerShutdownTask([&] {
        // NOTE: This function may be called at any time. It must not
        // depend on the prior execution of monger initializers or the
        // existence of threads.
        if (hasGlobalServiceContext()) {
            auto sc = getGlobalServiceContext();
            if (sc->getTransportLayer())
                sc->getTransportLayer()->shutdown();

            if (sc->getServiceEntryPoint()) {
                sc->getServiceEntryPoint()->endAllSessions(transport::Session::kEmptyTagMask);
                sc->getServiceEntryPoint()->shutdown(Seconds{10});
            }
        }
    });

    setupSignalHandlers();
    runGlobalInitializersOrDie(argc, argv, envp);
    startSignalProcessingThread(LogFileStatus::kNoLogFileToRotate);

    setGlobalServiceContext(ServiceContext::make());
    auto serviceContext = getGlobalServiceContext();
    serviceContext->setServiceEntryPoint(std::make_unique<ServiceEntryPointBridge>(serviceContext));
    serviceContext->setServiceExecutor(
        std::make_unique<transport::ServiceExecutorSynchronous>(serviceContext));

    fassert(50766, serviceContext->getServiceExecutor()->start());

    transport::TransportLayerASIO::Options opts;
    opts.ipList.emplace_back("0.0.0.0");
    opts.port = mongerBridgeGlobalParams.port;

    serviceContext->setTransportLayer(std::make_unique<monger::transport::TransportLayerASIO>(
        opts, serviceContext->getServiceEntryPoint()));
    auto tl = serviceContext->getTransportLayer();
    if (!tl->setup().isOK()) {
        log() << "Error setting up transport layer";
        return EXIT_NET_ERROR;
    }

    if (!tl->start().isOK()) {
        log() << "Error starting transport layer";
        return EXIT_NET_ERROR;
    }

    serviceContext->notifyStartupComplete();
    return waitForShutdown();
}

}  // namespace monger

#if defined(_WIN32)
// In Windows, wmain() is an alternate entry point for main(), and receives the same parameters
// as main() but encoded in Windows Unicode (UTF-16); "wide" 16-bit wchar_t characters.  The
// WindowsCommandLine object converts these wide character strings to a UTF-8 coded equivalent
// and makes them available through the argv() and envp() members.  This enables bridgeMain()
// to process UTF-8 encoded arguments and environment variables without regard to platform.
int wmain(int argc, wchar_t* argvW[], wchar_t* envpW[]) {
    monger::WindowsCommandLine wcl(argc, argvW, envpW);
    int exitCode = monger::bridgeMain(argc, wcl.argv(), wcl.envp());
    monger::quickExit(exitCode);
}
#else
int main(int argc, char* argv[], char** envp) {
    int exitCode = monger::bridgeMain(argc, argv, envp);
    monger::quickExit(exitCode);
}
#endif
