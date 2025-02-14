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

#include "monger/logger/log_component_settings.h"
#include "monger/logger/message_log_domain.h"

namespace monger {
namespace logger {

/**
 * Logging domain for ephemeral messages with minimum severity.
 */
class ComponentMessageLogDomain : public MessageLogDomain {
    ComponentMessageLogDomain(const ComponentMessageLogDomain&) = delete;
    ComponentMessageLogDomain& operator=(const ComponentMessageLogDomain&) = delete;

public:
    ComponentMessageLogDomain();

    ~ComponentMessageLogDomain();

    /**
     * Predicate that answers the question, "Should I, the caller, append to you, the log
     * domain, messages of the given severity?"  True means yes.
     */
    bool shouldLog(LogComponent component, LogSeverity severity) const;
    bool shouldLog(LogComponent component1, LogComponent component2, LogSeverity severity) const;
    bool shouldLog(LogComponent component1,
                   LogComponent component2,
                   LogComponent component3,
                   LogSeverity severity) const;

    /**
     * Returns true if a minimum log severity has been set for this component.
     * Called by log level commands to query component severity configuration.
     */
    bool hasMinimumLogSeverity(LogComponent component) const;

    /**
     * Gets the minimum severity of messages that should be sent to this LogDomain.
     */
    LogSeverity getMinimumLogSeverity() const;
    LogSeverity getMinimumLogSeverity(LogComponent component) const;

    /**
     * Sets the minimum severity of messages that should be sent to this LogDomain.
     */
    void setMinimumLoggedSeverity(LogSeverity severity);
    void setMinimumLoggedSeverity(LogComponent, LogSeverity severity);

    /**
     * Clears the minimum log severity for component.
     * For kDefault, severity level is initialized to default value.
     */
    void clearMinimumLoggedSeverity(LogComponent component);

    /**
     * Returns true if system logs should be redacted.
     */
    bool shouldRedactLogs() {
        return _shouldRedact.loadRelaxed();
    }

    /**
     * Set the 'redact' mode of the server.
     */
    void setShouldRedactLogs(bool shouldRedact);

private:
    LogComponentSettings _settings;
    AtomicWord<bool> _shouldRedact{false};
};

}  // namespace logger
}  // namespace monger
