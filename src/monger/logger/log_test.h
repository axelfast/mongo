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

#include <sstream>
#include <string>
#include <vector>

#include "monger/base/status.h"
#include "monger/logger/appender.h"
#include "monger/logger/log_severity.h"
#include "monger/logger/logger.h"
#include "monger/logger/message_log_domain.h"
#include "monger/unittest/unittest.h"

namespace monger {
namespace logger {

// Used for testing logging framework only.
// TODO(schwerin): Have logger write to a different log from the global log, so that tests can
// redirect their global log output for examination.
template <typename MessageEventEncoder>
class LogTest : public unittest::Test {
    friend class LogTestAppender;

public:
    LogTest() : _severityOld(globalLogDomain()->getMinimumLogSeverity()) {
        globalLogDomain()->clearAppenders();
        _appenderHandle =
            globalLogDomain()->attachAppender(std::make_unique<LogTestAppender>(this));
    }

    virtual ~LogTest() {
        globalLogDomain()->detachAppender(_appenderHandle);
        globalLogDomain()->setMinimumLoggedSeverity(_severityOld);
    }

protected:
    std::vector<std::string> _logLines;
    LogSeverity _severityOld;

private:
    class LogTestAppender : public MessageLogDomain::EventAppender {
    public:
        explicit LogTestAppender(LogTest* ltest) : _ltest(ltest) {}
        virtual ~LogTestAppender() {}
        virtual Status append(const MessageLogDomain::Event& event) {
            std::ostringstream _os;
            if (!_encoder.encode(event, _os))
                return Status(ErrorCodes::LogWriteFailed, "Failed to append to LogTestAppender.");
            _ltest->_logLines.push_back(_os.str());
            return Status::OK();
        }

    private:
        LogTest* _ltest;
        MessageEventEncoder _encoder;
    };

    MessageLogDomain::AppenderHandle _appenderHandle;
};

}  // namespace logger
}  // namespace monger
