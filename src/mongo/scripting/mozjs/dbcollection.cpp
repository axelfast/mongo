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

#include "monger/scripting/mozjs/dbcollection.h"

#include "monger/db/namespace_string.h"
#include "monger/db/operation_context.h"
#include "monger/scripting/mozjs/bson.h"
#include "monger/scripting/mozjs/db.h"
#include "monger/scripting/mozjs/implscope.h"
#include "monger/scripting/mozjs/objectwrapper.h"
#include "monger/scripting/mozjs/valuewriter.h"

namespace monger {
namespace mozjs {

const char* const DBCollectionInfo::className = "DBCollection";

void DBCollectionInfo::resolve(JSContext* cx,
                               JS::HandleObject obj,
                               JS::HandleId id,
                               bool* resolvedp) {
    DBInfo::resolve(cx, obj, id, resolvedp);
}

void DBCollectionInfo::construct(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    if (args.length() != 4)
        uasserted(ErrorCodes::BadValue, "collection constructor requires 4 arguments");

    for (unsigned i = 0; i < args.length(); ++i) {
        uassert(ErrorCodes::BadValue,
                "collection constructor called with undefined argument",
                !args.get(i).isUndefined());
    }

    JS::RootedObject thisv(cx);
    scope->getProto<DBCollectionInfo>().newObject(&thisv);
    ObjectWrapper o(cx, thisv);

    o.setValue(InternedString::_monger, args.get(0));
    o.setValue(InternedString::_db, args.get(1));
    o.setValue(InternedString::_shortName, args.get(2));
    o.setValue(InternedString::_fullName, args.get(3));

    args.rval().setObjectOrNull(thisv);
}

}  // namespace mozjs
}  // namespace monger
