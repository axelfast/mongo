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

#include "monger/db/mongerd_options.h"

#include <iostream>

#include "monger/util/exit_code.h"
#include "monger/util/options_parser/startup_option_init.h"
#include "monger/util/options_parser/startup_options.h"
#include "monger/util/quick_exit.h"

namespace monger {
MONGO_GENERAL_STARTUP_OPTIONS_REGISTER(MongerdOptions)(InitializerContext* context) {
    return addMongerdOptions(&moe::startupOptions);
}

MONGO_INITIALIZER_GENERAL(MongerdOptions,
                          ("BeginStartupOptionValidation", "AllFailPointsRegistered"),
                          ("EndStartupOptionValidation"))
(InitializerContext* context) {
    if (!handlePreValidationMongerdOptions(moe::startupOptionsParsed, context->args())) {
        quickExit(EXIT_SUCCESS);
    }
    // Run validation, but tell the Environment that we don't want it to be set as "valid",
    // since we may be making it invalid in the canonicalization process.
    Status ret = moe::startupOptionsParsed.validate(false /*setValid*/);
    if (!ret.isOK()) {
        return ret;
    }
    ret = validateMongerdOptions(moe::startupOptionsParsed);
    if (!ret.isOK()) {
        return ret;
    }
    ret = canonicalizeMongerdOptions(&moe::startupOptionsParsed);
    if (!ret.isOK()) {
        return ret;
    }
    ret = moe::startupOptionsParsed.validate();
    if (!ret.isOK()) {
        return ret;
    }
    return Status::OK();
}

MONGO_INITIALIZER_GENERAL(CoreOptions_Store,
                          ("BeginStartupOptionStorage"),
                          ("EndStartupOptionStorage"))
(InitializerContext* context) {
    Status ret = storeMongerdOptions(moe::startupOptionsParsed);
    if (!ret.isOK()) {
        std::cerr << ret.toString() << std::endl;
        std::cerr << "try '" << context->args()[0] << " --help' for more information" << std::endl;
        quickExit(EXIT_BADOPTIONS);
    }
    return Status::OK();
}

}  // namespace monger
