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

#include <iosfwd>
#include <string>

#include "monger/base/string_data.h"

namespace monger {
namespace logger {

/**
 * Representation of the severity / priority of a log message.
 *
 * Severities are totally ordered, from most severe to least severe as follows:
 * Severe, Error, Warning, Info, Log, Debug(1), Debug(2), ...
 */
class LogSeverity {
public:
    //
    // Static factory methods for getting LogSeverity objects of the various severity levels.
    //

    static inline LogSeverity Severe();
    static inline LogSeverity Error();
    static inline LogSeverity Warning();
    static inline LogSeverity Info();
    static inline LogSeverity Log();  // === Debug(0)

    static constexpr int kMaxDebugLevel = 5;

    // Construct a LogSeverity to represent the given debug level. Debug levels above
    // kMaxDebugLevel will be reset to kMaxDebugLevel.
    static inline LogSeverity Debug(int debugLevel);

    /**
     * Casts an integer to a severity.
     *
     * Do not use this.  It exists to enable a handful of leftover uses of LOG(0).
     */
    static inline LogSeverity cast(int);

    inline int toInt() const;

    /**
     * Returns a LogSeverity object that is one unit "more severe" than this one.
     */
    inline LogSeverity moreSevere() const;

    /**
     * Returns a LogSeverity object that is one unit "less severe" than this one.
     */
    inline LogSeverity lessSevere() const;

    /**
     * Returns a std::string naming this severity level.
     *
     * See toStringData(), below.
     */
    inline std::string toString() const;

    /**
     * Returns a StringData naming this security level.
     *
     * Not all levels are uniquely named.  Debug(N) is named "debug", regardless of "N",
     * e.g.
     */
    StringData toStringData() const;

    /**
     * Returns two characters naming this severity level. For non-debug levels, returns
     * a single character mapping to the first letter of the string returned by
     * `toStringData`, followed by a space. For debug levels, returns 'DN', where N
     * is an integer greater than zero.
     *
     * All levels are uniquely named.
     */
    StringData toStringDataCompact() const;

    //
    // Comparison operations.
    //

    /// Returns true if this is exactly as severe as other.
    inline bool operator==(const LogSeverity other) const;

    /// Returns true if this is not exactly as severe as other.
    inline bool operator!=(const LogSeverity other) const;

    /// Returns true if this is less severe than other.
    inline bool operator<(const LogSeverity other) const;

    /// Returns true if this is no more severe than other.
    inline bool operator<=(const LogSeverity other) const;

    /// Returns true if this is more severe than other.
    inline bool operator>(const LogSeverity other) const;

    /// Returns true if this is no less severe than other.
    inline bool operator>=(const LogSeverity other) const;

private:
    explicit LogSeverity(int severity) : _severity(severity) {}

    /// The stored severity.  More negative is more severe.  NOTE: This means that the >, <, >=
    /// and <= operators on LogSeverity have opposite sense of the same operators on the
    /// underlying integer.  That is, given severities S1 and S2, S1 > S2 means that S1.toInt()
    /// < S2.toInt().
    ///
    /// TODO(schwerin): Decide if we should change this so more positive is more severe.  The
    /// logLevel parameter in the database is more compatible with this sense, but it's not
    /// totally intuitive.  One could also remove the operator overloads in favor of named
    /// methods, isNoMoreSevereThan, isLessSevereThan, isMoreSevereThan, isNoLessSevereThan,
    /// isSameSeverity and isDifferentSeverity.
    int _severity;
};

std::ostream& operator<<(std::ostream& os, LogSeverity severity);

LogSeverity LogSeverity::Severe() {
    return LogSeverity(-4);
}
LogSeverity LogSeverity::Error() {
    return LogSeverity(-3);
}
LogSeverity LogSeverity::Warning() {
    return LogSeverity(-2);
}
LogSeverity LogSeverity::Info() {
    return LogSeverity(-1);
}
LogSeverity LogSeverity::Log() {
    return LogSeverity(0);
}
LogSeverity LogSeverity::Debug(int debugLevel) {
    // It would be appropriate to use std::max or std::clamp instead,
    // but it seems better not to drag in all of <algorithm> here.
    return LogSeverity(debugLevel > kMaxDebugLevel ? kMaxDebugLevel : debugLevel);
}

LogSeverity LogSeverity::cast(int ll) {
    return LogSeverity(ll);
}

int LogSeverity::toInt() const {
    return _severity;
}
LogSeverity LogSeverity::moreSevere() const {
    return LogSeverity(_severity - 1);
}
LogSeverity LogSeverity::lessSevere() const {
    return LogSeverity(_severity + 1);
}

bool LogSeverity::operator==(LogSeverity other) const {
    return _severity == other._severity;
}
bool LogSeverity::operator!=(LogSeverity other) const {
    return _severity != other._severity;
}
bool LogSeverity::operator<(LogSeverity other) const {
    return _severity > other._severity;
}
bool LogSeverity::operator<=(LogSeverity other) const {
    return _severity >= other._severity;
}
bool LogSeverity::operator>(LogSeverity other) const {
    return _severity < other._severity;
}
bool LogSeverity::operator>=(LogSeverity other) const {
    return _severity <= other._severity;
}

}  // namespace logger
}  // namespace monger
