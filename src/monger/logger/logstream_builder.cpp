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

#include "monger/logger/logstream_builder.h"

#include <memory>

#include "monger/base/init.h"
#include "monger/base/status.h"
#include "monger/logger/message_event_utf8_encoder.h"
#include "monger/logger/tee.h"
#include "monger/util/assert_util.h"  // TODO: remove apple dep for this in threadlocal.h
#include "monger/util/time_support.h"

namespace monger {

namespace {

/// This flag indicates whether the system providing a per-thread cache of ostringstreams
/// for use by LogstreamBuilder instances is initialized and ready for use.  Until this
/// flag is true, LogstreamBuilder instances must not use the cache.
bool isThreadOstreamCacheInitialized = false;

MONGO_INITIALIZER(LogstreamBuilder)(InitializerContext*) {
    isThreadOstreamCacheInitialized = true;
    return Status::OK();
}

thread_local std::unique_ptr<std::ostringstream> threadOstreamCache;

// During unittests, where we don't use quickExit(), static finalization may destroy the
// cache before its last use, so mark it as not initialized in that case.
// This must be after the definition of threadOstreamCache so that it is destroyed first.
struct ThreadOstreamCacheFinalizer {
    ~ThreadOstreamCacheFinalizer() {
        isThreadOstreamCacheInitialized = false;
    }
} threadOstreamCacheFinalizer;

}  // namespace

namespace logger {

LogstreamBuilder::LogstreamBuilder(MessageLogDomain* domain,
                                   StringData contextName,
                                   LogSeverity severity)
    : LogstreamBuilder(domain, contextName, std::move(severity), LogComponent::kDefault) {}

LogstreamBuilder::LogstreamBuilder(MessageLogDomain* domain,
                                   StringData contextName,
                                   LogSeverity severity,
                                   LogComponent component,
                                   bool shouldCache)
    : _domain(domain),
      _contextName(contextName.toString()),
      _severity(std::move(severity)),
      _component(std::move(component)),
      _tee(nullptr),
      _shouldCache(shouldCache) {}

LogstreamBuilder::~LogstreamBuilder() {
    if (_os) {
        if (!_baseMessage.empty())
            _baseMessage.push_back(' ');
        _baseMessage += _os->str();
        MessageEventEphemeral message(
            Date_t::now(), _severity, _component, _contextName, _baseMessage);
        message.setIsTruncatable(_isTruncatable);
        _domain->append(message).transitional_ignore();
        if (_tee) {
            _os->str("");
            logger::MessageEventDetailsEncoder teeEncoder;
            teeEncoder.encode(message, *_os);
            _tee->write(_os->str());
        }
        _os->str("");
        if (_shouldCache && isThreadOstreamCacheInitialized && !threadOstreamCache) {
            threadOstreamCache = std::move(_os);
        }
    }
}

void LogstreamBuilder::operator<<(Tee* tee) {
    makeStream();  // Adding a Tee counts for purposes of deciding to make a log message.
    // TODO: dassert(!_tee);
    _tee = tee;
}

void LogstreamBuilder::makeStream() {
    if (!_os) {
        if (_shouldCache && isThreadOstreamCacheInitialized && threadOstreamCache) {
            _os = std::move(threadOstreamCache);
        } else {
            _os = std::make_unique<std::ostringstream>();
        }
    }
}

}  // namespace logger
}  // namespace monger
