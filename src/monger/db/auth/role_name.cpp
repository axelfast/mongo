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

#include "monger/db/auth/role_name.h"

#include <algorithm>
#include <iostream>
#include <string>

#include "monger/base/string_data.h"
#include "monger/db/auth/authorization_manager.h"
#include "monger/util/assert_util.h"

namespace monger {

RoleName::RoleName(StringData role, StringData dbname) {
    _fullName.resize(role.size() + dbname.size() + 1);
    std::string::iterator iter =
        std::copy(role.rawData(), role.rawData() + role.size(), _fullName.begin());
    *iter = '@';
    ++iter;
    iter = std::copy(dbname.rawData(), dbname.rawData() + dbname.size(), iter);
    dassert(iter == _fullName.end());
    _splitPoint = role.size();
}

std::ostream& operator<<(std::ostream& os, const RoleName& name) {
    return os << name.getFullName();
}

RoleName RoleName::parseFromBSON(const BSONElement& elem) {
    auto obj = elem.embeddedObjectUserCheck();
    std::array<BSONElement, 2> fields;
    obj.getFields(
        {AuthorizationManager::ROLE_NAME_FIELD_NAME, AuthorizationManager::ROLE_DB_FIELD_NAME},
        &fields);
    const auto& nameField = fields[0];
    uassert(ErrorCodes::BadValue,
            str::stream() << "user name must contain a string field named: "
                          << AuthorizationManager::ROLE_NAME_FIELD_NAME,
            nameField.type() == String);

    const auto& dbField = fields[1];
    uassert(ErrorCodes::BadValue,
            str::stream() << "role name must contain a string field named: "
                          << AuthorizationManager::ROLE_DB_FIELD_NAME,
            nameField.type() == String);

    return RoleName(nameField.valueStringData(), dbField.valueStringData());
}

void RoleName::serializeToBSON(StringData fieldName, BSONObjBuilder* bob) const {
    BSONObjBuilder sub(bob->subobjStart(fieldName));
    _serializeToSubObj(&sub);
}

void RoleName::serializeToBSON(BSONArrayBuilder* bob) const {
    BSONObjBuilder sub(bob->subobjStart());
    _serializeToSubObj(&sub);
}

void RoleName::_serializeToSubObj(BSONObjBuilder* sub) const {
    sub->append(AuthorizationManager::ROLE_NAME_FIELD_NAME, getRole());
    sub->append(AuthorizationManager::ROLE_DB_FIELD_NAME, getDB());
}


}  // namespace monger
