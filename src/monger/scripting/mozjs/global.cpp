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

#include "monger/scripting/mozjs/global.h"

#include <js/Conversions.h>

#include "monger/base/init.h"
#include "monger/logger/logger.h"
#include "monger/logger/logstream_builder.h"
#include "monger/scripting/engine.h"
#include "monger/scripting/mozjs/implscope.h"
#include "monger/scripting/mozjs/jsstringwrapper.h"
#include "monger/scripting/mozjs/objectwrapper.h"
#include "monger/scripting/mozjs/valuereader.h"
#include "monger/scripting/mozjs/valuewriter.h"
#include "monger/util/version.h"

namespace monger {
namespace mozjs {

const JSFunctionSpec GlobalInfo::freeFunctions[7] = {
    MONGO_ATTACH_JS_FUNCTION(sleep),
    MONGO_ATTACH_JS_FUNCTION(gc),
    MONGO_ATTACH_JS_FUNCTION(print),
    MONGO_ATTACH_JS_FUNCTION(version),
    MONGO_ATTACH_JS_FUNCTION(buildInfo),
    MONGO_ATTACH_JS_FUNCTION(getJSHeapLimitMB),
    JS_FS_END,
};

const char* const GlobalInfo::className = "Global";

namespace {

logger::MessageLogDomain* jsPrintLogDomain;

}  // namespace

void GlobalInfo::Functions::print::call(JSContext* cx, JS::CallArgs args) {
    logger::LogstreamBuilder builder(jsPrintLogDomain, getThreadName(), logger::LogSeverity::Log());
    std::ostream& ss = builder.stream();

    bool first = true;
    for (size_t i = 0; i < args.length(); i++) {
        if (first)
            first = false;
        else
            ss << " ";

        if (args.get(i).isNullOrUndefined()) {
            // failed to get object to convert
            ss << "[unknown type]";
            continue;
        }

        JSStringWrapper jsstr(cx, JS::ToString(cx, args.get(i)));
        ss << jsstr.toStringData();
    }
    ss << std::endl;

    args.rval().setUndefined();
}

void GlobalInfo::Functions::version::call(JSContext* cx, JS::CallArgs args) {
    auto versionString = VersionInfoInterface::instance().version();
    ValueReader(cx, args.rval()).fromStringData(versionString);
}

void GlobalInfo::Functions::buildInfo::call(JSContext* cx, JS::CallArgs args) {
    BSONObjBuilder b;
    VersionInfoInterface::instance().appendBuildInfo(&b);
    ValueReader(cx, args.rval()).fromBSON(b.obj(), nullptr, false);
}

void GlobalInfo::Functions::getJSHeapLimitMB::call(JSContext* cx, JS::CallArgs args) {
    ValueReader(cx, args.rval()).fromDouble(monger::getGlobalScriptEngine()->getJSHeapLimitMB());
}

void GlobalInfo::Functions::gc::call(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    scope->gc();

    args.rval().setUndefined();
}

void GlobalInfo::Functions::sleep::call(JSContext* cx, JS::CallArgs args) {
    uassert(16259,
            "sleep takes a single numeric argument -- sleep(milliseconds)",
            args.length() == 1 && args.get(0).isNumber());

    auto scope = getScope(cx);
    int64_t duration = ValueWriter(cx, args.get(0)).toInt64();
    scope->sleep(Milliseconds(duration));

    args.rval().setUndefined();
}

MONGO_INITIALIZER(JavascriptPrintDomain)(InitializerContext*) {
    jsPrintLogDomain = logger::globalLogManager()->getNamedDomain("javascriptOutput");
    return Status::OK();
}

}  // namespace mozjs
}  // namespace monger
