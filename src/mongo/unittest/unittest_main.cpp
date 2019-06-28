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

#include <iostream>
#include <string>
#include <vector>

#include "monger/base/initializer.h"
#include "monger/base/status.h"
#include "monger/logger/logger.h"
#include "monger/unittest/unittest.h"
#include "monger/unittest/unittest_options_gen.h"
#include "monger/util/options_parser/environment.h"
#include "monger/util/options_parser/option_section.h"
#include "monger/util/options_parser/options_parser.h"
#include "monger/util/signal_handlers_synchronous.h"

using monger::Status;

namespace moe = ::monger::optionenvironment;

int main(int argc, char** argv, char** envp) {
    ::monger::clearSignalMask();
    ::monger::setupSynchronousSignalHandlers();

    ::monger::runGlobalInitializersOrDie(argc, argv, envp);

    moe::OptionSection options;

    Status status = monger::unittest::addUnitTestOptions(&options);
    if (!status.isOK()) {
        std::cerr << status;
        return EXIT_FAILURE;
    }

    moe::OptionsParser parser;
    moe::Environment environment;
    std::map<std::string, std::string> env;
    std::vector<std::string> argVector(argv, argv + argc);
    Status ret = parser.run(options, argVector, env, &environment);
    if (!ret.isOK()) {
        std::cerr << options.helpString();
        return EXIT_FAILURE;
    }

    bool list = false;
    moe::StringVector_t suites;
    std::string filter;
    int repeat = 1;
    std::string verbose;
    // "list" and "repeat" will be assigned with default values, if not present.
    invariant(environment.get("list", &list));
    invariant(environment.get("repeat", &repeat));
    // The default values of "suite" "filter" and "verbose" are empty.
    environment.get("suite", &suites).ignore();
    environment.get("filter", &filter).ignore();
    environment.get("verbose", &verbose).ignore();

    if (std::any_of(verbose.cbegin(), verbose.cend(), [](char ch) { return ch != 'v'; })) {
        std::cerr << "The string for the --verbose option cannot contain characters other than 'v'"
                  << std::endl;
        std::cerr << options.helpString();
        return EXIT_FAILURE;
    }
    ::monger::logger::globalLogDomain()->setMinimumLoggedSeverity(
        ::monger::logger::LogSeverity::Debug(verbose.length()));

    if (list) {
        auto suiteNames = ::monger::unittest::getAllSuiteNames();
        for (auto name : suiteNames) {
            std::cout << name << std::endl;
        }
        return EXIT_SUCCESS;
    }

    auto result = ::monger::unittest::Suite::run(suites, filter, repeat);

    ret = ::monger::runGlobalDeinitializers();
    if (!ret.isOK()) {
        std::cerr << "Global deinitilization failed: " << ret.reason() << std::endl;
    }

    return result;
}
