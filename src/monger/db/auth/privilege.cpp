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

#include "monger/db/auth/privilege.h"

#include "monger/db/auth/action_set.h"
#include "monger/db/auth/action_type.h"
#include "monger/db/auth/privilege_parser.h"

namespace monger {

void Privilege::addPrivilegeToPrivilegeVector(PrivilegeVector* privileges,
                                              const Privilege& privilegeToAdd) {
    for (PrivilegeVector::iterator it = privileges->begin(); it != privileges->end(); ++it) {
        if (it->getResourcePattern() == privilegeToAdd.getResourcePattern()) {
            it->addActions(privilegeToAdd.getActions());
            return;
        }
    }
    // No privilege exists yet for this resource
    privileges->push_back(privilegeToAdd);
}

void Privilege::addPrivilegesToPrivilegeVector(PrivilegeVector* privileges,
                                               const PrivilegeVector& privilegesToAdd) {
    for (auto&& priv : privilegesToAdd) {
        addPrivilegeToPrivilegeVector(privileges, priv);
    }
}

Privilege::Privilege(const ResourcePattern& resource, const ActionType& action)
    : _resource(resource) {
    _actions.addAction(action);
}
Privilege::Privilege(const ResourcePattern& resource, const ActionSet& actions)
    : _resource(resource), _actions(actions) {}

void Privilege::addActions(const ActionSet& actionsToAdd) {
    _actions.addAllActionsFromSet(actionsToAdd);
}

void Privilege::removeActions(const ActionSet& actionsToRemove) {
    _actions.removeAllActionsFromSet(actionsToRemove);
}

bool Privilege::includesAction(const ActionType& action) const {
    return _actions.contains(action);
}

bool Privilege::includesActions(const ActionSet& actions) const {
    return _actions.isSupersetOf(actions);
}

BSONObj Privilege::toBSON() const {
    ParsedPrivilege pp;
    std::string errmsg;
    invariant(ParsedPrivilege::privilegeToParsedPrivilege(*this, &pp, &errmsg));
    return pp.toBSON();
}

Status Privilege::getBSONForPrivileges(const PrivilegeVector& privileges,
                                       mutablebson::Element resultArray) try {
    for (auto& currPriv : privileges) {
        std::string errmsg;
        ParsedPrivilege privilege;
        if (!ParsedPrivilege::privilegeToParsedPrivilege(currPriv, &privilege, &errmsg)) {
            return Status(ErrorCodes::BadValue, errmsg);
        }
        uassertStatusOK(resultArray.appendObject("privileges", privilege.toBSON()));
    }
    return Status::OK();
} catch (...) {
    return exceptionToStatus();
}

}  // namespace monger
