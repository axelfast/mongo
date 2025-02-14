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

#include <string>
#include <vector>

#include "monger/bson/mutable/element.h"
#include "monger/db/auth/privilege.h"
#include "monger/db/auth/role_name.h"
#include "monger/db/auth/user_name.h"

namespace monger {

class AuthorizationManager;
class AuthorizationSession;
struct BSONArray;
class BSONObj;
class Client;
class OperationContext;

namespace auth {

//
// checkAuthorizedTo* methods
//

Status checkAuthorizedToGrantRoles(AuthorizationSession* authzSession,
                                   const std::vector<RoleName>& roles);

Status checkAuthorizedToGrantPrivileges(AuthorizationSession* authzSession,
                                        const PrivilegeVector& privileges);

Status checkAuthorizedToRevokeRoles(AuthorizationSession* authzSession,
                                    const std::vector<RoleName>& roles);

Status checkAuthorizedToRevokePrivileges(AuthorizationSession* authzSession,
                                         const PrivilegeVector& privileges);

//
// checkAuthFor*Command methods
//

Status checkAuthForCreateUserCommand(Client* client,
                                     const std::string& dbname,
                                     const BSONObj& cmdObj);

Status checkAuthForUpdateUserCommand(Client* client,
                                     const std::string& dbname,
                                     const BSONObj& cmdObj);

Status checkAuthForGrantRolesToUserCommand(Client* client,
                                           const std::string& dbname,
                                           const BSONObj& cmdObj);

Status checkAuthForCreateRoleCommand(Client* client,
                                     const std::string& dbname,
                                     const BSONObj& cmdObj);

Status checkAuthForUpdateRoleCommand(Client* client,
                                     const std::string& dbname,
                                     const BSONObj& cmdObj);

Status checkAuthForGrantRolesToRoleCommand(Client* client,
                                           const std::string& dbname,
                                           const BSONObj& cmdObj);

Status checkAuthForGrantPrivilegesToRoleCommand(Client* client,
                                                const std::string& dbname,
                                                const BSONObj& cmdObj);

Status checkAuthForDropAllUsersFromDatabaseCommand(Client* client, const std::string& dbname);

Status checkAuthForRevokeRolesFromUserCommand(Client* client,
                                              const std::string& dbname,
                                              const BSONObj& cmdObj);

Status checkAuthForRevokeRolesFromRoleCommand(Client* client,
                                              const std::string& dbname,
                                              const BSONObj& cmdObj);

Status checkAuthForDropUserCommand(Client* client,
                                   const std::string& dbname,
                                   const BSONObj& cmdObj);

Status checkAuthForDropRoleCommand(Client* client,
                                   const std::string& dbname,
                                   const BSONObj& cmdObj);


Status checkAuthForUsersInfoCommand(Client* client,
                                    const std::string& dbname,
                                    const BSONObj& cmdObj);

Status checkAuthForRevokePrivilegesFromRoleCommand(Client* client,
                                                   const std::string& dbname,
                                                   const BSONObj& cmdObj);

Status checkAuthForDropAllRolesFromDatabaseCommand(Client* client, const std::string& dbname);

Status checkAuthForRolesInfoCommand(Client* client,
                                    const std::string& dbname,
                                    const BSONObj& cmdObj);

Status checkAuthForInvalidateUserCacheCommand(Client* client);

Status checkAuthForGetUserCacheGenerationCommand(Client* client);

Status checkAuthForMergeAuthzCollectionsCommand(Client* client, const BSONObj& cmdObj);

}  // namespace auth
}  // namespace monger
