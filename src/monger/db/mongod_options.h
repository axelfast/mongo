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

#include <boost/optional.hpp>

#include "monger/base/status.h"
#include "monger/db/repl/repl_settings.h"
#include "monger/db/server_options.h"
#include "monger/db/storage/storage_options.h"
#include "monger/util/options_parser/environment.h"
#include "monger/util/options_parser/option_section.h"

namespace monger {

namespace optionenvironment {
class OptionSection;
class Environment;
}  // namespace optionenvironment

namespace moe = monger::optionenvironment;


Status addMongerdOptions(moe::OptionSection* options);

void printMongerdHelp(const moe::OptionSection& options);

/**
 * Handle options that should come before validation, such as "help".
 *
 * Returns false if an option was found that implies we should prematurely exit with success.
 */
bool handlePreValidationMongerdOptions(const moe::Environment& params,
                                      const std::vector<std::string>& args);

/**
 * Handle custom validation of mongerd options that can not currently be done by using
 * Constraints in the Environment.  See the "validate" function in the Environment class for
 * more details.
 */
Status validateMongerdOptions(const moe::Environment& params);

/**
 * Canonicalize mongerd options for the given environment.
 *
 * For example, the options "dur", "nodur", "journal", "nojournal", and
 * "storage.journaling.enabled" should all be merged into "storage.journaling.enabled".
 */
Status canonicalizeMongerdOptions(moe::Environment* params);

// Must be called after "storeMongerdOptions"
StatusWith<repl::ReplSettings> parseMongerdReplicationOptions(const moe::Environment& params);

Status storeMongerdOptions(const moe::Environment& params);

/**
 * Help test user for storage.dbPath config option.
 */
std::string storageDBPathDescription();
}
