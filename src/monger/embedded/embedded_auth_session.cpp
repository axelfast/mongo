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

#include "monger/db/auth/authorization_session.h"
#include "monger/embedded/not_implemented.h"
#include "monger/util/assert_util.h"

namespace monger {
namespace embedded {
namespace {

class Impl : public UserNameIterator::Impl {
    bool more() const override {
        return false;
    }
    const UserName& get() const override {
        UASSERT_NOT_IMPLEMENTED;
    }

    const UserName& next() override {
        UASSERT_NOT_IMPLEMENTED;
    }

    Impl* doClone() const override {
        return new Impl(*this);
    }
};

class AuthorizationSession : public monger::AuthorizationSession {
public:
    explicit AuthorizationSession(AuthorizationManager* const authzManager)
        : _authzManager(authzManager) {}

    AuthorizationManager& getAuthorizationManager() override {
        return *_authzManager;
    }

    void startRequest(OperationContext*) override {
        // It is always okay to start a request in embedded.
    }

    Status addAndAuthorizeUser(OperationContext*, const UserName&) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    User* lookupUser(const UserName&) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    User* getSingleUser() override {
        UASSERT_NOT_IMPLEMENTED;
    }

    bool isAuthenticated() override {
        // It should always be okay to check whether you're authenticated, but on embedded
        // it should always return false
        return false;
    }

    UserNameIterator getAuthenticatedUserNames() override {
        return UserNameIterator(std::make_unique<Impl>());
    }

    RoleNameIterator getAuthenticatedRoleNames() override {
        UASSERT_NOT_IMPLEMENTED;
    }

    std::string getAuthenticatedUserNamesToken() override {
        UASSERT_NOT_IMPLEMENTED;
    }

    void grantInternalAuthorization(Client* client) override {
        // Always okay to do something, on embedded.
    }

    void grantInternalAuthorization(OperationContext* opCtx) override {
        // Always okay to do something, on embedded.
    }

    void logoutDatabase(OperationContext* opCtx, const StringData) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    PrivilegeVector getDefaultPrivileges() override {
        UASSERT_NOT_IMPLEMENTED;
    }

    Status checkAuthForFind(const NamespaceString&, bool) override {
        return Status::OK();
    }

    Status checkAuthForGetMore(const NamespaceString&, long long, bool) override {
        return Status::OK();
    }

    Status checkAuthForUpdate(OperationContext*,
                              const NamespaceString&,
                              const BSONObj&,
                              const write_ops::UpdateModification&,
                              bool) override {
        return Status::OK();
    }

    Status checkAuthForInsert(OperationContext*, const NamespaceString&) override {
        return Status::OK();
    }

    Status checkAuthForDelete(OperationContext*, const NamespaceString&, const BSONObj&) override {
        return Status::OK();
    }

    Status checkAuthForKillCursors(const NamespaceString&, UserNameIterator) override {
        return Status::OK();
    }

    StatusWith<PrivilegeVector> getPrivilegesForAggregate(const NamespaceString&,
                                                          const BSONObj&,
                                                          bool) override {
        return PrivilegeVector();
    }

    Status checkAuthForCreate(const NamespaceString&, const BSONObj&, bool) override {
        return Status::OK();
    }

    Status checkAuthForCollMod(const NamespaceString&, const BSONObj&, bool) override {
        return Status::OK();
    }

    Status checkAuthorizedToGrantPrivilege(const Privilege&) override {
        return Status::OK();
    }

    Status checkAuthorizedToRevokePrivilege(const Privilege&) override {
        return Status::OK();
    }

    StatusWith<PrivilegeVector> checkAuthorizedToListCollections(StringData,
                                                                 const BSONObj&) override {
        return PrivilegeVector();
    }

    bool isUsingLocalhostBypass() override {
        return false;
    }

    bool isAuthorizedToParseNamespaceElement(const BSONElement&) override {
        return true;
    }

    bool isAuthorizedToCreateRole(const auth::CreateOrUpdateRoleArgs&) override {
        return true;
    }

    bool isAuthorizedToGrantRole(const RoleName&) override {
        return true;
    }

    bool isAuthorizedToRevokeRole(const RoleName&) override {
        return true;
    }

    bool isAuthorizedToChangeAsUser(const UserName&, ActionType) override {
        return true;
    }

    bool isAuthorizedToChangeOwnPasswordAsUser(const UserName&) override {
        return true;
    }

    bool isAuthorizedToChangeOwnCustomDataAsUser(const UserName&) override {
        return true;
    }

    bool isAuthenticatedAsUserWithRole(const RoleName&) override {
        return true;
    }

    bool isAuthorizedForPrivilege(const Privilege&) override {
        return true;
    }

    bool isAuthorizedForPrivileges(const std::vector<Privilege>&) override {
        return true;
    }

    bool isAuthorizedForActionsOnResource(const ResourcePattern&, ActionType) override {
        return true;
    }

    bool isAuthorizedForActionsOnResource(const ResourcePattern&, const ActionSet&) override {
        return true;
    }

    bool isAuthorizedForActionsOnNamespace(const NamespaceString&, ActionType) override {
        return true;
    }

    bool isAuthorizedForActionsOnNamespace(const NamespaceString&, const ActionSet&) override {
        return true;
    }

    void setImpersonatedUserData(std::vector<UserName>, std::vector<RoleName>) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    UserNameIterator getImpersonatedUserNames() override {
        UASSERT_NOT_IMPLEMENTED;
    }

    RoleNameIterator getImpersonatedRoleNames() override {
        UASSERT_NOT_IMPLEMENTED;
    }

    void clearImpersonatedUserData() override {
        UASSERT_NOT_IMPLEMENTED;
    }

    bool isCoauthorizedWithClient(Client*, WithLock opClientLock) override {
        return true;
    }

    bool isCoauthorizedWith(UserNameIterator) override {
        return true;
    }

    bool isImpersonating() const override {
        UASSERT_NOT_IMPLEMENTED;
    }

    Status checkCursorSessionPrivilege(OperationContext*,
                                       boost::optional<LogicalSessionId>) override {
        return Status::OK();
    }

    bool isAuthorizedForAnyActionOnAnyResourceInDB(StringData) override {
        return true;
    }

    bool isAuthorizedForAnyActionOnResource(const ResourcePattern&) override {
        return true;
    }

protected:
    std::tuple<std::vector<UserName>*, std::vector<RoleName>*> _getImpersonations() override {
        UASSERT_NOT_IMPLEMENTED;
    }

private:
    AuthorizationManager* const _authzManager;
};

}  // namespace
}  // namespace embedded

MONGO_REGISTER_SHIM(AuthorizationSession::create)
(AuthorizationManager* const authzManager)->std::unique_ptr<AuthorizationSession> {
    return std::make_unique<embedded::AuthorizationSession>(authzManager);
}

}  // namespace monger
