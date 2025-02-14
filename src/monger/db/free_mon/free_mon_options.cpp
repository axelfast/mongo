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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kFTDC


#include "monger/platform/basic.h"

#include "monger/db/free_mon/free_mon_options.h"

#include "monger/base/error_codes.h"
#include "monger/base/initializer_context.h"
#include "monger/base/status.h"
#include "monger/base/status_with.h"
#include "monger/base/string_data.h"
#include "monger/util/log.h"
#include "monger/util/options_parser/startup_option_init.h"
#include "monger/util/options_parser/startup_options.h"

namespace monger {

FreeMonParams globalFreeMonParams;

namespace optionenvironment {
class OptionSection;
class Environment;
}  // namespace optionenvironment

namespace moe = monger::optionenvironment;

namespace {

constexpr StringData kEnableCloudState_on = "on"_sd;
constexpr StringData kEnableCloudState_off = "off"_sd;
constexpr StringData kEnableCloudState_runtime = "runtime"_sd;

StatusWith<EnableCloudStateEnum> EnableCloudState_parse(StringData value) {
    if (value == kEnableCloudState_on) {
        return EnableCloudStateEnum::kOn;
    }
    if (value == kEnableCloudState_off) {
        return EnableCloudStateEnum::kOff;
    }
    if (value == kEnableCloudState_runtime) {
        return EnableCloudStateEnum::kRuntime;
    }

    return Status(ErrorCodes::InvalidOptions, "Unrecognized state");
}

Status storeFreeMonitoringOptions(const moe::Environment& params) {

    if (params.count("cloud.monitoring.free.state")) {
        auto swState =
            EnableCloudState_parse(params["cloud.monitoring.free.state"].as<std::string>());
        if (!swState.isOK()) {
            return swState.getStatus();
        }
        globalFreeMonParams.freeMonitoringState = swState.getValue();
    }

    if (params.count("cloud.monitoring.free.tags")) {
        globalFreeMonParams.freeMonitoringTags =
            params["cloud.monitoring.free.tags"].as<std::vector<std::string>>();
    }

    return Status::OK();
}

MONGO_STARTUP_OPTIONS_STORE(FreeMonitoringOptions)(InitializerContext* /*unused*/) {
    return storeFreeMonitoringOptions(moe::startupOptionsParsed);
}

}  // namespace
}  // namespace monger
