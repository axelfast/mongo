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

#include "monger/bson/util/bson_extract.h"
#include "monger/db/auth/authorization_manager.h"
#include "monger/db/auth/authorization_session.h"
#include "monger/db/commands.h"

namespace monger {

using std::string;
using std::stringstream;

class CmdConnectionStatus : public BasicCommand {
public:
    CmdConnectionStatus() : BasicCommand("connectionStatus") {}
    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }
    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }
    bool requiresAuth() const override {
        return false;
    }
    virtual void addRequiredPrivileges(const std::string& dbname,
                                       const BSONObj& cmdObj,
                                       std::vector<Privilege>* out) const {}  // No auth required

    std::string help() const override {
        return "Returns connection-specific information such as logged-in users and their roles";
    }

    bool run(OperationContext* opCtx,
             const string&,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) {
        AuthorizationSession* authSession = AuthorizationSession::get(Client::getCurrent());

        bool showPrivileges;
        Status status =
            bsonExtractBooleanFieldWithDefault(cmdObj, "showPrivileges", false, &showPrivileges);
        uassertStatusOK(status);

        BSONObjBuilder authInfo(result.subobjStart("authInfo"));
        {
            BSONArrayBuilder authenticatedUsers(authInfo.subarrayStart("authenticatedUsers"));
            UserNameIterator nameIter = authSession->getAuthenticatedUserNames();

            for (; nameIter.more(); nameIter.next()) {
                BSONObjBuilder userInfoBuilder(authenticatedUsers.subobjStart());
                userInfoBuilder.append(AuthorizationManager::USER_NAME_FIELD_NAME,
                                       nameIter->getUser());
                userInfoBuilder.append(AuthorizationManager::USER_DB_FIELD_NAME, nameIter->getDB());
            }
        }
        {
            BSONArrayBuilder authenticatedRoles(authInfo.subarrayStart("authenticatedUserRoles"));
            RoleNameIterator roleIter = authSession->getAuthenticatedRoleNames();

            for (; roleIter.more(); roleIter.next()) {
                BSONObjBuilder roleInfoBuilder(authenticatedRoles.subobjStart());
                roleInfoBuilder.append(AuthorizationManager::ROLE_NAME_FIELD_NAME,
                                       roleIter->getRole());
                roleInfoBuilder.append(AuthorizationManager::ROLE_DB_FIELD_NAME, roleIter->getDB());
            }
        }
        if (showPrivileges) {
            BSONArrayBuilder authenticatedPrivileges(
                authInfo.subarrayStart("authenticatedUserPrivileges"));

            // Create a unified map of resources to privileges, to avoid duplicate
            // entries in the connection status output.
            User::ResourcePrivilegeMap unifiedResourcePrivilegeMap;
            UserNameIterator nameIter = authSession->getAuthenticatedUserNames();

            for (; nameIter.more(); nameIter.next()) {
                User* authUser = authSession->lookupUser(*nameIter);
                const User::ResourcePrivilegeMap& resourcePrivilegeMap = authUser->getPrivileges();
                for (User::ResourcePrivilegeMap::const_iterator it = resourcePrivilegeMap.begin();
                     it != resourcePrivilegeMap.end();
                     ++it) {
                    if (unifiedResourcePrivilegeMap.find(it->first) ==
                        unifiedResourcePrivilegeMap.end()) {
                        unifiedResourcePrivilegeMap[it->first] = it->second;
                    } else {
                        unifiedResourcePrivilegeMap[it->first].addActions(it->second.getActions());
                    }
                }
            }

            for (User::ResourcePrivilegeMap::const_iterator it =
                     unifiedResourcePrivilegeMap.begin();
                 it != unifiedResourcePrivilegeMap.end();
                 ++it) {
                authenticatedPrivileges << it->second.toBSON();
            }
        }

        authInfo.doneFast();

        return true;
    }
} cmdConnectionStatus;
}
