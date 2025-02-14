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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kQuery

#include "monger/platform/basic.h"

#include "monger/scripting/mozjs/valuereader.h"

#include <cmath>
#include <cstdio>
#include <js/CharacterEncoding.h>
#include <js/Date.h>

#include "monger/base/error_codes.h"
#include "monger/platform/decimal128.h"
#include "monger/scripting/mozjs/implscope.h"
#include "monger/scripting/mozjs/objectwrapper.h"
#include "monger/util/base64.h"
#include "monger/util/log.h"

namespace monger {
namespace mozjs {

ValueReader::ValueReader(JSContext* cx, JS::MutableHandleValue value)
    : _context(cx), _value(value) {}

void ValueReader::fromBSONElement(const BSONElement& elem, const BSONObj& parent, bool readOnly) {
    auto scope = getScope(_context);

    switch (elem.type()) {
        case monger::Code:
            // javascriptProtection prevents Code and CodeWScope BSON types from
            // being automatically marshalled into executable functions.
            if (scope->isJavaScriptProtectionEnabled()) {
                JS::AutoValueArray<1> args(_context);
                ValueReader(_context, args[0]).fromStringData(elem.valueStringData());

                JS::RootedObject obj(_context);
                scope->getProto<CodeInfo>().newInstance(args, _value);
            } else {
                scope->newFunction(elem.valueStringData(), _value);
            }
            return;
        case monger::CodeWScope:
            if (scope->isJavaScriptProtectionEnabled()) {
                JS::AutoValueArray<2> args(_context);

                ValueReader(_context, args[0]).fromStringData(elem.codeWScopeCode());
                ValueReader(_context, args[1]).fromBSON(elem.codeWScopeObject(), nullptr, readOnly);

                scope->getProto<CodeInfo>().newInstance(args, _value);
            } else {
                if (!elem.codeWScopeObject().isEmpty())
                    warning() << "CodeWScope doesn't transfer to db.eval";
                scope->newFunction(StringData(elem.codeWScopeCode(), elem.codeWScopeCodeLen() - 1),
                                   _value);
            }
            return;
        case monger::Symbol:
        case monger::String:
            fromStringData(elem.valueStringData());
            return;
        case monger::jstOID: {
            OIDInfo::make(_context, elem.OID(), _value);
            return;
        }
        case monger::NumberDouble:
            fromDouble(elem.Number());
            return;
        case monger::NumberInt:
            _value.setInt32(elem.Int());
            return;
        case monger::Array: {
            fromBSONArray(elem.embeddedObject(), &parent, readOnly);
            return;
        }
        case monger::Object:
            fromBSON(elem.embeddedObject(), &parent, readOnly);
            return;
        case monger::Date:
            _value.setObjectOrNull(
                JS::NewDateObject(_context, JS::TimeClip(elem.Date().toMillisSinceEpoch())));
            return;
        case monger::Bool:
            _value.setBoolean(elem.Bool());
            return;
        case monger::jstNULL:
            _value.setNull();
            return;
        case monger::EOO:
        case monger::Undefined:
            _value.setUndefined();
            return;
        case monger::RegEx: {
            // TODO parse into a custom type that can support any patterns and flags SERVER-9803

            JS::AutoValueArray<2> args(_context);

            ValueReader(_context, args[0]).fromStringData(elem.regex());
            ValueReader(_context, args[1]).fromStringData(elem.regexFlags());

            JS::RootedObject obj(_context);
            scope->getProto<RegExpInfo>().newInstance(args, &obj);

            _value.setObjectOrNull(obj);

            return;
        }
        case monger::BinData: {
            int len;
            const char* data = elem.binData(len);
            std::stringstream ss;
            base64::encode(ss, data, len);

            JS::AutoValueArray<2> args(_context);

            args[0].setInt32(elem.binDataType());

            ValueReader(_context, args[1]).fromStringData(ss.str());

            scope->getProto<BinDataInfo>().newInstance(args, _value);
            return;
        }
        case monger::bsonTimestamp: {
            JS::AutoValueArray<2> args(_context);

            ValueReader(_context, args[0])
                .fromDouble(elem.timestampTime().toMillisSinceEpoch() / 1000);
            ValueReader(_context, args[1]).fromDouble(elem.timestampInc());

            scope->getProto<TimestampInfo>().newInstance(args, _value);

            return;
        }
        case monger::NumberLong: {
            JS::RootedObject thisv(_context);
            scope->getProto<NumberLongInfo>().newObject(&thisv);
            JS_SetPrivate(thisv, scope->trackedNew<int64_t>(elem.numberLong()));
            _value.setObjectOrNull(thisv);
            return;
        }
        case monger::NumberDecimal: {
            Decimal128 decimal = elem.numberDecimal();
            JS::AutoValueArray<1> args(_context);
            ValueReader(_context, args[0]).fromDecimal128(decimal);
            JS::RootedObject obj(_context);

            scope->getProto<NumberDecimalInfo>().newInstance(args, &obj);
            _value.setObjectOrNull(obj);

            return;
        }
        case monger::MinKey:
            scope->getProto<MinKeyInfo>().newInstance(_value);
            return;
        case monger::MaxKey:
            scope->getProto<MaxKeyInfo>().newInstance(_value);
            return;
        case monger::DBRef: {
            JS::AutoValueArray<1> oidArgs(_context);
            ValueReader(_context, oidArgs[0]).fromStringData(elem.dbrefOID().toString());

            JS::AutoValueArray<2> dbPointerArgs(_context);
            ValueReader(_context, dbPointerArgs[0]).fromStringData(elem.dbrefNS());
            scope->getProto<OIDInfo>().newInstance(oidArgs, dbPointerArgs[1]);

            scope->getProto<DBPointerInfo>().newInstance(dbPointerArgs, _value);
            return;
        }
        default:
            massert(16661,
                    str::stream() << "can't handle type: " << elem.type() << " " << elem.toString(),
                    false);
            break;
    }

    _value.setUndefined();
}

void ValueReader::fromBSON(const BSONObj& obj, const BSONObj* parent, bool readOnly) {
    JS::RootedObject child(_context);

    bool filledDBRef = false;
    if (obj.firstElementType() == String && (obj.firstElementFieldNameStringData() == "$ref")) {
        BSONObjIterator it(obj);
        it.next();
        const BSONElement id = it.next();

        if (id.ok() && id.fieldNameStringData() == "$id") {
            DBRefInfo::make(_context, &child, obj, parent, readOnly);
            filledDBRef = true;
        }
    }

    if (!filledDBRef) {
        BSONInfo::make(_context, &child, obj, parent, readOnly);
    }

    _value.setObjectOrNull(child);
}

void ValueReader::fromBSONArray(const BSONObj& obj, const BSONObj* parent, bool readOnly) {
    JS::AutoValueVector avv(_context);

    BSONForEach(elem, obj) {
        JS::RootedValue member(_context);

        ValueReader(_context, &member).fromBSONElement(elem, parent ? *parent : obj, readOnly);
        if (!avv.append(member)) {
            uasserted(ErrorCodes::JSInterpreterFailure, "Failed to append to JS array");
        }
    }
    JS::RootedObject array(_context, JS_NewArrayObject(_context, avv));
    if (!array) {
        uasserted(ErrorCodes::JSInterpreterFailure, "Failed to JS_NewArrayObject");
    }
    _value.setObjectOrNull(array);
}

/**
 * SpiderMonkey doesn't have a direct entry point to create a jsstring from
 * utf8, so we have to flow through some slightly less public interfaces.
 *
 * Basically, we have to use their routines to convert to utf16, then assign
 * those bytes with JS_NewUCStringCopyN
 */
void ValueReader::fromStringData(StringData sd) {
    size_t utf16Len;

    // TODO: we have tests that involve dropping garbage in. Do we want to
    //       throw, or to take the lossy conversion?
    auto utf16 = JS::LossyUTF8CharsToNewTwoByteCharsZ(
        _context, JS::UTF8Chars(sd.rawData(), sd.size()), &utf16Len);

    mozilla::UniquePtr<char16_t, JS::FreePolicy> utf16Deleter(utf16.get());

    uassert(ErrorCodes::JSInterpreterFailure,
            str::stream() << "Failed to encode \"" << sd << "\" as utf16",
            utf16);

    auto jsStr = JS_NewUCStringCopyN(_context, utf16.get(), utf16Len);

    uassert(ErrorCodes::JSInterpreterFailure,
            str::stream() << "Unable to copy \"" << sd << "\" into MozJS",
            jsStr);

    _value.setString(jsStr);
}

/**
 * SpiderMonkey doesn't have a direct entry point to create a Decimal128
 *
 * Read NumberDecimal as a string
 * Note: This prevents shell arithmetic, which is performed for number longs
 * by converting them to doubles, which is imprecise. Until there is a better
 * method to handle non-double shell arithmetic, decimals will remain
 * as a non-numeric js type.
 */
void ValueReader::fromDecimal128(Decimal128 decimal) {
    NumberDecimalInfo::make(_context, _value, decimal);
}

/**
 * SpiderMonkey has a nasty habit of interpreting certain NaN patterns as other boxed types (it
 * assumes that only one kind of NaN exists in JS, rather than the full ieee754 spectrum).  Thus we
 * have to flow all double setting through a wrapper which ensures that nan's are coerced to the
 * canonical javascript NaN.
 *
 * See SERVER-24054 for more details.
 */
void ValueReader::fromDouble(double d) {
    if (std::isnan(d)) {
        _value.set(JS_GetNaNValue(_context));
    } else {
        _value.setDouble(d);
    }
}

void ValueReader::fromInt64(int64_t i) {
    auto scope = getScope(_context);
    JS::RootedObject num(_context);
    scope->getProto<NumberLongInfo>().newObject(&num);
    JS_SetPrivate(num, scope->trackedNew<int64_t>(i));
    _value.setObjectOrNull(num);
}

}  // namespace mozjs
}  // namespace monger
