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

#include "monger/base/init.h"
#include "monger/db/mongerd_options.h"
#include "monger/db/mongerd_options_general_gen.h"
#include "monger/db/mongerd_options_replication_gen.h"
#include "monger/db/service_context.h"
#include "monger/embedded/embedded.h"
#include "monger/embedded/embedded_options.h"
#include "monger/embedded/embedded_options_helpers.h"
#include "monger/embedded/service_entry_point_embedded.h"
#include "monger/transport/service_entry_point_impl.h"
#include "monger/transport/transport_layer.h"
#include "monger/transport/transport_layer_manager.h"
#include "monger/util/exit.h"
#include "monger/util/log.h"
#include "monger/util/options_parser/options_parser.h"
#include "monger/util/options_parser/startup_options.h"
#include "monger/util/signal_handlers.h"
#include "monger/util/text.h"

#include <yaml-cpp/yaml.h>

namespace monger {
namespace {

class ServiceEntryPointMongere : public ServiceEntryPointImpl {
public:
    explicit ServiceEntryPointMongere(ServiceContext* svcCtx)
        : ServiceEntryPointImpl(svcCtx),
          _sepEmbedded(std::make_unique<ServiceEntryPointEmbedded>()) {}

    DbResponse handleRequest(OperationContext* opCtx, const Message& request) final {
        return _sepEmbedded->handleRequest(opCtx, request);
    }

private:
    std::unique_ptr<ServiceEntryPointEmbedded> _sepEmbedded;
};

MONGO_INITIALIZER_WITH_PREREQUISITES(SignalProcessingStartup, ("ThreadNameInitializer"))
(InitializerContext*) {
    // Make sure we call this as soon as possible but before any other threads are started. Before
    // embedded::initialize is too early and after is too late. So instead we hook in during the
    // global initialization at the right place.
    startSignalProcessingThread();
    return Status::OK();
}

int mongeredMain(int argc, char* argv[], char** envp) {
    ServiceContext* serviceContext = nullptr;

    registerShutdownTask([&]() {
        if (!serviceContext)
            return;

        if (auto tl = serviceContext->getTransportLayer()) {
            log(logger::LogComponent::kNetwork) << "shutdown: going to close listening sockets...";
            tl->shutdown();
        }

        if (auto sep = serviceContext->getServiceEntryPoint()) {
            if (sep->shutdown(Seconds(10))) {
                embedded::shutdown(serviceContext);
            } else {
                log(logger::LogComponent::kNetwork) << "Failed to shutdown service entry point "
                                                       "within timelimit, skipping embedded "
                                                       "shutdown.";
            }
        }
    });

    setupSignalHandlers();

    log() << "MongerDB embedded standalone application, for testing purposes only";

    try {
        optionenvironment::OptionSection startupOptions("Options");
        // Adding all options mongerd we don't have to maintain a separate set for this executable,
        // some will be unused but that's fine as this is just an executable for testing purposes
        // anyway.
        uassertStatusOK(addMongerdGeneralOptions(&startupOptions));
        uassertStatusOK(addMongerdReplicationOptions(&startupOptions));
        uassertStatusOK(embedded::addOptions(&startupOptions));
        uassertStatusOK(
            embedded_integration_helpers::parseCommandLineOptions(argc, argv, startupOptions));

        serviceContext = embedded::initialize("");

        // storeMongerdOptions() triggers cmdline censoring, which must happen after initializers.
        uassertStatusOK(storeMongerdOptions(optionenvironment::startupOptionsParsed));

        // Override the ServiceEntryPoint with one that can support transport layers.
        serviceContext->setServiceEntryPoint(
            std::make_unique<ServiceEntryPointMongere>(serviceContext));

        auto tl =
            transport::TransportLayerManager::createWithConfig(&serverGlobalParams, serviceContext);
        uassertStatusOK(tl->setup());

        serviceContext->setTransportLayer(std::move(tl));

        uassertStatusOK(serviceContext->getServiceExecutor()->start());
        uassertStatusOK(serviceContext->getTransportLayer()->start());
    } catch (const std::exception& ex) {
        error() << ex.what();
        return EXIT_BADOPTIONS;
    }

    return waitForShutdown();
}

}  // namespace
}  // namespace monger

#if defined(_WIN32)
// In Windows, wmain() is an alternate entry point for main(), and receives the same parameters
// as main() but encoded in Windows Unicode (UTF-16); "wide" 16-bit wchar_t characters.  The
// WindowsCommandLine object converts these wide character strings to a UTF-8 coded equivalent
// and makes them available through the argv() and envp() members.  This enables mongerDbMain()
// to process UTF-8 encoded arguments and environment variables without regard to platform.
int wmain(int argc, wchar_t* argvW[], wchar_t* envpW[]) {
    monger::WindowsCommandLine wcl(argc, argvW, envpW);
    return monger::mongeredMain(argc, wcl.argv(), wcl.envp());
}
#else
int main(int argc, char* argv[], char** envp) {
    return monger::mongeredMain(argc, argv, envp);
}
#endif
