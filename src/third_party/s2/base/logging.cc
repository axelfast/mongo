// Copyright 2010 Google
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kGeo

#include "logging.h"

#include <utility>

#include "monger/util/assert_util.h"
#include "monger/util/log.h"
#include "monger/util/str.h"

using ::monger::logger::LogstreamBuilder;

LogMessageBase::LogMessageBase(LogstreamBuilder builder, const char* file, int line) :
    _lsb(std::move(builder)) {
    _lsb.setBaseMessage(::monger::str::stream() << file << ':' << line << ": ");
}

LogMessageBase::LogMessageBase(LogstreamBuilder builder) : _lsb(std::move(builder)) { }

LogMessageInfo::LogMessageInfo() : LogMessageBase(monger::log()) { }

LogMessageWarning::LogMessageWarning(const char* file, int line) :
        LogMessageBase(monger::warning(), file, line) { }

LogMessageFatal::LogMessageFatal(const char* file, int line) :
        LogMessageBase(monger::severe(), file, line) { }

#pragma warning(push)
// C4722: 'LogMessageFatal::~LogMessageFatal': destructor never returns, potential memory leak
#pragma warning(disable : 4722)
LogMessageFatal::~LogMessageFatal() {
    _lsb.~LogstreamBuilder();
    fassertFailed(40048);
}
#pragma warning(pop)
