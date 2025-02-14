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

#include "monger/platform/basic.h"

#include "monger/logger/component_message_log_domain.h"

namespace monger {
namespace logger {

ComponentMessageLogDomain::ComponentMessageLogDomain() {}

ComponentMessageLogDomain::~ComponentMessageLogDomain() {}

bool ComponentMessageLogDomain::hasMinimumLogSeverity(LogComponent component) const {
    return _settings.hasMinimumLogSeverity(component);
}

bool ComponentMessageLogDomain::shouldLog(LogComponent component, LogSeverity severity) const {
    return _settings.shouldLog(component, severity);
}

bool ComponentMessageLogDomain::shouldLog(LogComponent component1,
                                          LogComponent component2,
                                          LogSeverity severity) const {
    return _settings.shouldLog(component1, severity) || _settings.shouldLog(component2, severity);
}

bool ComponentMessageLogDomain::shouldLog(LogComponent component1,
                                          LogComponent component2,
                                          LogComponent component3,
                                          LogSeverity severity) const {
    return _settings.shouldLog(component1, severity) || _settings.shouldLog(component2, severity) ||
        _settings.shouldLog(component3, severity);
}

LogSeverity ComponentMessageLogDomain::getMinimumLogSeverity() const {
    return _settings.getMinimumLogSeverity(LogComponent::kDefault);
}

LogSeverity ComponentMessageLogDomain::getMinimumLogSeverity(LogComponent component) const {
    return _settings.getMinimumLogSeverity(component);
}

void ComponentMessageLogDomain::setMinimumLoggedSeverity(LogSeverity severity) {
    _settings.setMinimumLoggedSeverity(LogComponent::kDefault, severity);
}

void ComponentMessageLogDomain::setMinimumLoggedSeverity(LogComponent component,
                                                         LogSeverity severity) {
    _settings.setMinimumLoggedSeverity(component, severity);
}

void ComponentMessageLogDomain::clearMinimumLoggedSeverity(LogComponent component) {
    _settings.clearMinimumLoggedSeverity(component);
}

void ComponentMessageLogDomain::setShouldRedactLogs(bool shouldRedact) {
    _shouldRedact.store(shouldRedact);
}

}  // namespace logger
}  // namespace monger
