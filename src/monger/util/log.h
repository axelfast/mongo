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

// #pragma once is not used in this header.
// This header attempts to enforce the rule that no logging should be done in
// an inline function defined in a header.
// To enforce this "no logging in header" rule, we use #include guards with a validating #else
// clause.
// Also, this header relies on a preprocessor macro to determine the default component for the
// unconditional logging functions severe(), error(), warning() and log(). Disallowing multiple
// inclusion of log.h will ensure that the default component will be set correctly.

#if defined(MONGO_UTIL_LOG_H_)
#error \
    "monger/util/log.h cannot be included multiple times. " \
       "This may occur when log.h is included in a header. " \
       "Please check your #include's."
#else  // MONGO_UTIL_LOG_H_
#define MONGO_UTIL_LOG_H_

#include "monger/base/status.h"
#include "monger/bson/util/builder.h"
#include "monger/logger/log_component.h"
#include "monger/logger/log_severity_limiter.h"
#include "monger/logger/logger.h"
#include "monger/logger/logstream_builder.h"
#include "monger/logger/redaction.h"
#include "monger/logger/tee.h"
#include "monger/util/concurrency/thread_name.h"
#include "monger/util/errno_util.h"

// Provide log component in global scope so that MONGO_LOG will always have a valid component.
// Global log component will be kDefault unless overridden by MONGO_LOG_DEFAULT_COMPONENT.
#if defined(MONGO_LOG_DEFAULT_COMPONENT)
const ::monger::logger::LogComponent MongerLogDefaultComponent_component =
    MONGO_LOG_DEFAULT_COMPONENT;
#else
#error \
    "monger/util/log.h requires MONGO_LOG_DEFAULT_COMPONENT to be defined. " \
       "Please see http://www.mongerdb.org/about/contributors/reference/server-logging-rules/ "
#endif  // MONGO_LOG_DEFAULT_COMPONENT

namespace monger {

namespace logger {
typedef void (*ExtraLogContextFn)(BufBuilder& builder);
Status registerExtraLogContextFn(ExtraLogContextFn contextFn);

}  // namespace logger

namespace {

using logger::LogstreamBuilder;
using logger::Tee;

/**
 * Returns a LogstreamBuilder for logging a message with LogSeverity::Severe().
 */
inline LogstreamBuilder severe() {
    return LogstreamBuilder(logger::globalLogDomain(),
                            getThreadName(),
                            logger::LogSeverity::Severe(),
                            ::MongerLogDefaultComponent_component);
}

inline LogstreamBuilder severe(logger::LogComponent component) {
    return LogstreamBuilder(
        logger::globalLogDomain(), getThreadName(), logger::LogSeverity::Severe(), component);
}

/**
 * Returns a LogstreamBuilder for logging a message with LogSeverity::Error().
 */
inline LogstreamBuilder error() {
    return LogstreamBuilder(logger::globalLogDomain(),
                            getThreadName(),
                            logger::LogSeverity::Error(),
                            ::MongerLogDefaultComponent_component);
}

inline LogstreamBuilder error(logger::LogComponent component) {
    return LogstreamBuilder(
        logger::globalLogDomain(), getThreadName(), logger::LogSeverity::Error(), component);
}

/**
 * Returns a LogstreamBuilder for logging a message with LogSeverity::Warning().
 */
inline LogstreamBuilder warning() {
    return LogstreamBuilder(logger::globalLogDomain(),
                            getThreadName(),
                            logger::LogSeverity::Warning(),
                            ::MongerLogDefaultComponent_component);
}

inline LogstreamBuilder warning(logger::LogComponent component) {
    return LogstreamBuilder(
        logger::globalLogDomain(), getThreadName(), logger::LogSeverity::Warning(), component);
}

/**
 * Returns a LogstreamBuilder for logging a message with LogSeverity::Log().
 */
inline LogstreamBuilder log() {
    return LogstreamBuilder(logger::globalLogDomain(),
                            getThreadName(),
                            logger::LogSeverity::Log(),
                            ::MongerLogDefaultComponent_component);
}

/**
 * Returns a LogstreamBuilder that does not cache its ostream in a threadlocal cache.
 * Use this variant when logging from places that may not be able to access threadlocals,
 * such as from within other threadlocal-managed objects, or thread_specific_ptr-managed
 * objects.
 *
 * Once SERVER-29377 is completed, this overload can be removed.
 */
inline LogstreamBuilder logNoCache() {
    return LogstreamBuilder(logger::globalLogDomain(),
                            getThreadName(),
                            logger::LogSeverity::Log(),
                            ::MongerLogDefaultComponent_component,
                            false);
}

inline LogstreamBuilder log(logger::LogComponent component) {
    return LogstreamBuilder(
        logger::globalLogDomain(), getThreadName(), logger::LogSeverity::Log(), component);
}

inline LogstreamBuilder log(logger::LogComponent::Value componentValue) {
    return LogstreamBuilder(
        logger::globalLogDomain(), getThreadName(), logger::LogSeverity::Log(), componentValue);
}

/**
 * Runs the same logic as log()/warning()/error(), without actually outputting a stream.
 */
inline bool shouldLog(logger::LogComponent logComponent, logger::LogSeverity severity) {
    return logger::globalLogDomain()->shouldLog(logComponent, severity);
}

inline bool shouldLog(logger::LogSeverity severity) {
    return shouldLog(::MongerLogDefaultComponent_component, severity);
}

}  // namespace

// MONGO_LOG uses log component from MongerLogDefaultComponent from current or global namespace.
#define MONGO_LOG(DLEVEL)                                                              \
    if (!(::monger::logger::globalLogDomain())                                          \
             ->shouldLog(MongerLogDefaultComponent_component,                           \
                         ::monger::LogstreamBuilder::severityCast(DLEVEL))) {           \
    } else                                                                             \
    ::monger::logger::LogstreamBuilder(::monger::logger::globalLogDomain(),              \
                                      ::monger::getThreadName(),                        \
                                      ::monger::LogstreamBuilder::severityCast(DLEVEL), \
                                      MongerLogDefaultComponent_component)

#define LOG MONGO_LOG

#define MONGO_LOG_COMPONENT(DLEVEL, COMPONENT1)                                            \
    if (!(::monger::logger::globalLogDomain())                                              \
             ->shouldLog((COMPONENT1), ::monger::LogstreamBuilder::severityCast(DLEVEL))) { \
    } else                                                                                 \
    ::monger::logger::LogstreamBuilder(::monger::logger::globalLogDomain(),                  \
                                      ::monger::getThreadName(),                            \
                                      ::monger::LogstreamBuilder::severityCast(DLEVEL),     \
                                      (COMPONENT1))

#define MONGO_LOG_COMPONENT2(DLEVEL, COMPONENT1, COMPONENT2)                                     \
    if (!(::monger::logger::globalLogDomain())                                                    \
             ->shouldLog(                                                                        \
                 (COMPONENT1), (COMPONENT2), ::monger::LogstreamBuilder::severityCast(DLEVEL))) { \
    } else                                                                                       \
    ::monger::logger::LogstreamBuilder(::monger::logger::globalLogDomain(),                        \
                                      ::monger::getThreadName(),                                  \
                                      ::monger::LogstreamBuilder::severityCast(DLEVEL),           \
                                      (COMPONENT1))

#define MONGO_LOG_COMPONENT3(DLEVEL, COMPONENT1, COMPONENT2, COMPONENT3)               \
    if (!(::monger::logger::globalLogDomain())                                          \
             ->shouldLog((COMPONENT1),                                                 \
                         (COMPONENT2),                                                 \
                         (COMPONENT3),                                                 \
                         ::monger::LogstreamBuilder::severityCast(DLEVEL))) {           \
    } else                                                                             \
    ::monger::logger::LogstreamBuilder(::monger::logger::globalLogDomain(),              \
                                      ::monger::getThreadName(),                        \
                                      ::monger::LogstreamBuilder::severityCast(DLEVEL), \
                                      (COMPONENT1))

/**
 * Rotates the log files.  Returns true if all logs rotate successfully.
 *
 * renameFiles - true means we rename files, false means we expect the file to be renamed
 *               externally
 *
 * logrotate on *nix systems expects us not to rename the file, it is expected that the program
 * simply open the file again with the same name.
 * We expect logrotate to rename the existing file before we rotate, and so the next open
 * we do should result in a file create.
 */
bool rotateLogs(bool renameFiles);

extern Tee* const warnings;            // Things put here go in serverStatus
extern Tee* const startupWarningsLog;  // Things put here get reported in MMS

/**
 * Write the current context (backtrace), along with the optional "msg".
 */
void logContext(const char* msg = nullptr);

/**
 * Turns the global log manager into a plain console logger (no adornments).
 */
void setPlainConsoleLogger();

}  // namespace monger

#endif  // MONGO_UTIL_LOG_H_
