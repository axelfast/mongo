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

#include <jsapi.h>

#include "monger/scripting/mozjs/wraptype.h"

namespace monger {
namespace mozjs {

/**
 * The "NumberLong" Javascript object.
 *
 * Represents a 64 integer with a JS representation like:
 *
 * {
 *     top         : Double,
 *     bottom      : Double,
 *     floatApprox : Double,
 * }
 *
 * Where top is the high 32 bits, bottom the low 32 bits and floatApprox a
 * floating point approximation.
 */
struct NumberLongInfo : public BaseInfo {
    static void construct(JSContext* cx, JS::CallArgs args);
    static void finalize(js::FreeOp* fop, JSObject* obj);

    struct Functions {
        MONGO_DECLARE_JS_FUNCTION(toNumber);
        MONGO_DECLARE_JS_FUNCTION(toString);
        MONGO_DECLARE_JS_FUNCTION(toJSON);
        MONGO_DECLARE_JS_FUNCTION(valueOf);
        MONGO_DECLARE_JS_FUNCTION(compare);
        MONGO_DECLARE_JS_FUNCTION(floatApprox);
        MONGO_DECLARE_JS_FUNCTION(top);
        MONGO_DECLARE_JS_FUNCTION(bottom);
    };

    static const JSFunctionSpec methods[6];

    static const char* const className;
    static const unsigned classFlags = JSCLASS_HAS_PRIVATE;

    static void postInstall(JSContext* cx, JS::HandleObject global, JS::HandleObject proto);

    static int64_t ToNumberLong(JSContext* cx, JS::HandleObject object);
    static int64_t ToNumberLong(JSContext* cx, JS::HandleValue value);
};

}  // namespace mozjs
}  // namespace monger
