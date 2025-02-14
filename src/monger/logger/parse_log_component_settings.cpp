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

#include "monger/logger/parse_log_component_settings.h"

#include <vector>

#include "monger/bson/bsonobj.h"
#include "monger/bson/bsonobjbuilder.h"
#include "monger/bson/bsontypes.h"
#include "monger/logger/log_component.h"
#include "monger/util/assert_util.h"
#include "monger/util/log.h"
#include "monger/util/str.h"

namespace monger {
namespace logger {

/*
 * Looks up a component by its short name, or returns kNumLogComponents
 * if the shortName is invalid
 */
const LogComponent _getComponentForShortName(StringData shortName) {
    for (int i = 0; i < int(LogComponent::kNumLogComponents); ++i) {
        LogComponent component = static_cast<LogComponent::Value>(i);
        if (component.getShortName() == shortName)
            return component;
    }
    return static_cast<LogComponent::Value>(LogComponent::kNumLogComponents);
}

StatusWith<std::vector<LogComponentSetting>> parseLogComponentSettings(const BSONObj& settings) {
    typedef std::vector<LogComponentSetting> Result;

    std::vector<LogComponentSetting> levelsToSet;
    std::vector<BSONObjIterator> iterators;

    LogComponent parentComponent = LogComponent::kDefault;
    BSONObjIterator iter(settings);

    while (iter.moreWithEOO()) {
        BSONElement elem = iter.next();
        if (elem.eoo()) {
            if (!iterators.empty()) {
                iter = iterators.back();
                iterators.pop_back();
                parentComponent = parentComponent.parent();
            }
            continue;
        }
        if (elem.fieldNameStringData() == "verbosity") {
            if (!elem.isNumber()) {
                return StatusWith<Result>(ErrorCodes::BadValue,
                                          str::stream() << "Expected "
                                                        << parentComponent.getDottedName()
                                                        << ".verbosity to be a number, but found "
                                                        << typeName(elem.type()));
            }
            levelsToSet.push_back((LogComponentSetting(parentComponent, elem.numberInt())));
            continue;
        }
        const StringData shortName = elem.fieldNameStringData();
        const LogComponent curr = _getComponentForShortName(shortName);

        if (curr == LogComponent::kNumLogComponents || curr.parent() != parentComponent) {
            return StatusWith<Result>(
                ErrorCodes::BadValue,
                str::stream() << "Invalid component name " << parentComponent.getDottedName() << "."
                              << shortName);
        }
        if (elem.isNumber()) {
            levelsToSet.push_back(LogComponentSetting(curr, elem.numberInt()));
            continue;
        }
        if (elem.type() != Object) {
            return StatusWith<Result>(ErrorCodes::BadValue,
                                      str::stream() << "Invalid type " << typeName(elem.type())
                                                    << "for component "
                                                    << parentComponent.getDottedName()
                                                    << "."
                                                    << shortName);
        }
        iterators.push_back(iter);
        parentComponent = curr;
        iter = BSONObjIterator(elem.Obj());
    }

    // Done walking settings
    return StatusWith<Result>(levelsToSet);
}

}  // namespace logger
}  // namespace monger
