/**
 *    Copyright (C) 2018-present MongerDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongerDB, Inc.
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

#include "monger/scripting/mozjs/mongerhelpers.h"

#include <jsapi.h>

#include "monger/scripting/engine.h"
#include "monger/scripting/mozjs/implscope.h"
#include "monger/scripting/mozjs/objectwrapper.h"
#include "monger/scripting/mozjs/valuereader.h"
#include "monger/scripting/mozjs/valuewriter.h"

namespace monger {
namespace JSFiles {
extern const JSFile mongerhelpers;
}  // namespace JSFiles

namespace mozjs {

const char* const MongerHelpersInfo::className = "MongerHelpers";

namespace {
const char kExportsObjectName[] = "exportToMongerHelpers";
const char kReflectName[] = "Reflect";
}  // namespace

std::string parseJSFunctionOrExpression(JSContext* cx, const StringData input) {
    JS::RootedValue jsStrOut(cx);
    JS::RootedValue jsStrIn(cx);

    ValueReader(cx, &jsStrIn).fromStringData(input);
    ObjectWrapper helpersWrapper(cx, getScope(cx)->getProto<MongerHelpersInfo>().getProto());

    helpersWrapper.callMethod("functionExpressionParser", JS::HandleValueArray(jsStrIn), &jsStrOut);

    return ValueWriter(cx, jsStrOut).toString();
}

void MongerHelpersInfo::postInstall(JSContext* cx, JS::HandleObject global, JS::HandleObject proto) {
    ObjectWrapper protoWrapper(cx, proto);
    ObjectWrapper globalWrapper(cx, global);

    // Initialize the reflection API and move it under the MongerHelpers object
    uassert(ErrorCodes::JSInterpreterFailure,
            "Error initializing javascript reflection API",
            JS_InitReflectParse(cx, global));
    JS::RootedValue reflectValue(cx);
    globalWrapper.getValue(kReflectName, &reflectValue);
    globalWrapper.deleteProperty(kReflectName);
    protoWrapper.setValue(kReflectName, reflectValue);

    JS::RootedValue exports(cx);
    getScope(cx)->execSetup(JSFiles::mongerhelpers);
    globalWrapper.getValue(kExportsObjectName, &exports);
    globalWrapper.deleteProperty(kExportsObjectName);

    ObjectWrapper exportsWrapper(cx, exports);
    JS::RootedValue copyExport(cx);
    exportsWrapper.enumerate([&](JS::HandleId _id) {
        exportsWrapper.getValue(_id, &copyExport);
        protoWrapper.setValue(_id, copyExport);
        return true;
    });
}

}  // namespace mozjs
}  // namespace monger
