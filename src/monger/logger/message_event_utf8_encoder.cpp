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

#include "monger/logger/message_event_utf8_encoder.h"

#include <iostream>

#include "monger/util/time_support.h"

namespace monger {

namespace logger {

const int LogContext::kDefaultMaxLogSizeKB;

LogContext::LogContext()
    : _dateFormatter{outputDateAsISOStringLocal}, _maxLogSizeSource{nullptr} {};

LogContext& MessageEventDetailsEncoder::getGlobalLogContext() {
    static LogContext context;
    return context;
}

void MessageEventDetailsEncoder::setMaxLogSizeKBSource(const AtomicWord<int>& source) {
    invariant(getGlobalLogContext()._maxLogSizeSource == nullptr);
    getGlobalLogContext()._maxLogSizeSource = &source;
}

int MessageEventDetailsEncoder::getMaxLogSizeKB() {
    auto* source = getGlobalLogContext()._maxLogSizeSource;

    // If not initialized, use the default
    if (source == nullptr)
        return LogContext::kDefaultMaxLogSizeKB;

    // If initialized, use the reference
    // TODO: This seems like a CST seq'd load we don't need. `loadRelaxed()`?
    return source->load();
}

void MessageEventDetailsEncoder::setDateFormatter(DateFormatter dateFormatter) {
    getGlobalLogContext()._dateFormatter = dateFormatter;
}

DateFormatter MessageEventDetailsEncoder::getDateFormatter() {
    return getGlobalLogContext()._dateFormatter;
}

namespace {
#ifdef _WIN32
constexpr auto kEOL = "\r\n"_sd;
#else
constexpr auto kEOL = "\n"_sd;
#endif
}  // namespace

MessageEventDetailsEncoder::~MessageEventDetailsEncoder() {}
std::ostream& MessageEventDetailsEncoder::encode(const MessageEventEphemeral& event,
                                                 std::ostream& os) {
    const auto maxLogSizeKB = getMaxLogSizeKB();

    const size_t maxLogSize = maxLogSizeKB * 1024;

    getDateFormatter()(os, event.getDate());
    os << ' ';

    const auto severity = event.getSeverity();
    os << severity.toStringDataCompact();
    os << ' ';

    LogComponent component = event.getComponent();
    os << component;
    os << ' ';

    StringData contextName = event.getContextName();
    if (!contextName.empty()) {
        os << '[' << contextName << "] ";
    }

    StringData msg = event.getMessage();

#ifdef _WIN32
    // We need to translate embedded Unix style line endings into Windows style endings.
    std::string tempstr;
    size_t embeddedNewLine = msg.find('\n');

    if (embeddedNewLine != std::string::npos) {
        tempstr = msg.toString().replace(embeddedNewLine, 1, "\r\n");

        embeddedNewLine = tempstr.find('\n', embeddedNewLine + 2);
        while (embeddedNewLine != std::string::npos) {
            tempstr = tempstr.replace(embeddedNewLine, 1, "\r\n");

            embeddedNewLine = tempstr.find('\n', embeddedNewLine + 2);
        }

        msg = tempstr;
    }
#endif

    if (event.isTruncatable() && msg.size() > maxLogSize) {
        os << "warning: log line attempted (" << msg.size() / 1024 << "kB) over max size ("
           << maxLogSizeKB << "kB), printing beginning and end ... ";
        os << msg.substr(0, maxLogSize / 3);
        os << " .......... ";
        os << msg.substr(msg.size() - (maxLogSize / 3));
    } else {
        os << msg;
    }

    if (!msg.endsWith(kEOL))
        os << kEOL;

    return os;
}

MessageEventWithContextEncoder::~MessageEventWithContextEncoder() {}
std::ostream& MessageEventWithContextEncoder::encode(const MessageEventEphemeral& event,
                                                     std::ostream& os) {
    LogComponent component = event.getComponent();
    os << component;
    os << ' ';

    StringData contextName = event.getContextName();
    if (!contextName.empty()) {
        os << '[' << contextName << "] ";
    }
    StringData message = event.getMessage();
    os << message;
    if (!message.endsWith("\n"))
        os << '\n';
    return os;
}

MessageEventUnadornedEncoder::~MessageEventUnadornedEncoder() {}
std::ostream& MessageEventUnadornedEncoder::encode(const MessageEventEphemeral& event,
                                                   std::ostream& os) {
    StringData message = event.getMessage();
    os << message;
    if (!message.endsWith("\n"))
        os << '\n';
    return os;
}

}  // namespace logger
}  // namespace monger
