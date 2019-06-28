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

#include "monger/scripting/mozjs/numberint.h"

#include "monger/scripting/mozjs/implscope.h"
#include "monger/scripting/mozjs/objectwrapper.h"
#include "monger/scripting/mozjs/valuereader.h"
#include "monger/scripting/mozjs/valuewriter.h"
#include "monger/scripting/mozjs/wrapconstrainedmethod.h"
#include "monger/util/str.h"

namespace monger {
namespace mozjs {

const JSFunctionSpec NumberIntInfo::methods[5] = {
    MONGO_ATTACH_JS_CONSTRAINED_METHOD(toNumber, NumberIntInfo),
    MONGO_ATTACH_JS_CONSTRAINED_METHOD(toString, NumberIntInfo),
    MONGO_ATTACH_JS_CONSTRAINED_METHOD(toJSON, NumberIntInfo),
    MONGO_ATTACH_JS_CONSTRAINED_METHOD(valueOf, NumberIntInfo),
    JS_FS_END,
};

const char* const NumberIntInfo::className = "NumberInt";

void NumberIntInfo::finalize(js::FreeOp* fop, JSObject* obj) {
    auto x = static_cast<int*>(JS_GetPrivate(obj));

    if (x)
        getScope(fop)->trackedDelete(x);
}

int NumberIntInfo::ToNumberInt(JSContext* cx, JS::HandleValue thisv) {
    auto x = static_cast<int*>(JS_GetPrivate(thisv.toObjectOrNull()));

    return x ? *x : 0;
}

int NumberIntInfo::ToNumberInt(JSContext* cx, JS::HandleObject thisv) {
    auto x = static_cast<int*>(JS_GetPrivate(thisv));

    return x ? *x : 0;
}

void NumberIntInfo::Functions::valueOf::call(JSContext* cx, JS::CallArgs args) {
    int out = NumberIntInfo::ToNumberInt(cx, args.thisv());

    args.rval().setInt32(out);
}

void NumberIntInfo::Functions::toNumber::call(JSContext* cx, JS::CallArgs args) {
    valueOf::call(cx, args);
}

void NumberIntInfo::Functions::toString::call(JSContext* cx, JS::CallArgs args) {
    int val = NumberIntInfo::ToNumberInt(cx, args.thisv());

    str::stream ss;
    ss << "NumberInt(" << val << ")";

    ValueReader(cx, args.rval()).fromStringData(ss.operator std::string());
}

void NumberIntInfo::Functions::toJSON::call(JSContext* cx, JS::CallArgs args) {
    int val = NumberIntInfo::ToNumberInt(cx, args.thisv());

    args.rval().setInt32(val);
}

void NumberIntInfo::construct(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    JS::RootedObject thisv(cx);

    scope->getProto<NumberIntInfo>().newObject(&thisv);

    int32_t x = 0;

    if (args.length() == 0) {
        // Do nothing
    } else if (args.length() == 1) {
        x = ValueWriter(cx, args.get(0)).toInt32();
    } else {
        uasserted(ErrorCodes::BadValue, "NumberInt takes 0 or 1 arguments");
    }

    JS_SetPrivate(thisv, scope->trackedNew<int>(x));

    args.rval().setObjectOrNull(thisv);
}

}  // namespace mozjs
}  // namespace monger
