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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kDefault

#include "monger/platform/basic.h"

#include <iostream>
#include <string>
#include <vector>

#include "monger/base/initializer.h"
#include "monger/client/connection_string.h"
#include "monger/db/service_context.h"
#include "monger/transport/transport_layer_asio.h"
#include "monger/unittest/unittest.h"
#include "monger/util/log.h"
#include "monger/util/options_parser/environment.h"
#include "monger/util/options_parser/option_section.h"
#include "monger/util/options_parser/options_parser.h"
#include "monger/util/options_parser/startup_option_init.h"
#include "monger/util/options_parser/startup_options.h"
#include "monger/util/quick_exit.h"
#include "monger/util/signal_handlers_synchronous.h"

using namespace monger;

namespace {

ConnectionString fixtureConnectionString{};

}  // namespace

namespace monger {
namespace unittest {

ConnectionString getFixtureConnectionString() {
    return fixtureConnectionString;
}

}  // namespace unittest
}  // namespace monger

int main(int argc, char** argv, char** envp) {
    setupSynchronousSignalHandlers();
    runGlobalInitializersOrDie(argc, argv, envp);
    setGlobalServiceContext(ServiceContext::make());
    quickExit(unittest::Suite::run(std::vector<std::string>(), "", 1));
}

namespace moe = monger::optionenvironment;

MONGO_STARTUP_OPTIONS_VALIDATE(IntegrationTestOptions)(InitializerContext*) {
    auto& env = moe::startupOptionsParsed;
    auto& opts = moe::startupOptions;

    auto ret = env.validate();

    if (!ret.isOK()) {
        return ret;
    }

    if (env.count("help")) {
        std::cout << opts.helpString() << std::endl;
        quickExit(EXIT_SUCCESS);
    }

    return Status::OK();
}

MONGO_STARTUP_OPTIONS_STORE(IntegrationTestOptions)(InitializerContext*) {
    const auto& env = moe::startupOptionsParsed;

    std::string connectionString = env["connectionString"].as<std::string>();

    auto swConnectionString = ConnectionString::parse(connectionString);
    if (!swConnectionString.isOK()) {
        return swConnectionString.getStatus();
    }

    fixtureConnectionString = std::move(swConnectionString.getValue());
    log() << "Using test fixture with connection string = " << connectionString;


    return Status::OK();
}
