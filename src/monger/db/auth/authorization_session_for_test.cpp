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

#include "monger/db/auth/authorization_session_for_test.h"

#include <algorithm>
#include <memory>

#include "monger/db/auth/privilege.h"
#include "monger/db/auth/user.h"
#include "monger/db/auth/user_name.h"
#include "monger/db/auth/user_set.h"

namespace monger {
constexpr StringData AuthorizationSessionForTest::kTestDBName;

AuthorizationSessionForTest::~AuthorizationSessionForTest() {
    revokeAllPrivileges();
}

void AuthorizationSessionForTest::assumePrivilegesForDB(Privilege privilege, StringData dbName) {
    assumePrivilegesForDB(std::vector<Privilege>{privilege}, dbName);
}

void AuthorizationSessionForTest::assumePrivilegesForDB(PrivilegeVector privileges,
                                                        StringData dbName) {
    auto user = std::make_shared<User>(UserName("authorizationSessionForTestUser", dbName));
    user->addPrivileges(privileges);

    _authenticatedUsers.add(user);
    _testUsers.emplace_back(std::move(user));
    _buildAuthenticatedRolesVector();
}

void AuthorizationSessionForTest::revokePrivilegesForDB(StringData dbName) {
    _authenticatedUsers.removeByDBName(dbName);
    _testUsers.erase(
        std::remove_if(_testUsers.begin(),
                       _testUsers.end(),
                       [&](const auto& user) { return dbName == user->getName().getDB(); }),
        _testUsers.end());
}

void AuthorizationSessionForTest::revokeAllPrivileges() {
    _testUsers.erase(std::remove_if(_testUsers.begin(),
                                    _testUsers.end(),
                                    [&](const auto& user) {
                                        _authenticatedUsers.removeByDBName(user->getName().getDB());
                                        return true;
                                    }),
                     _testUsers.end());
}
}  // namespace monger
