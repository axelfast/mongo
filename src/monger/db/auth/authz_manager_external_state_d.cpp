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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kAccessControl

#include "monger/platform/basic.h"

#include "monger/db/auth/authz_manager_external_state_d.h"

#include <memory>

#include "monger/base/status.h"
#include "monger/db/auth/authz_session_external_state_d.h"
#include "monger/db/auth/user_name.h"
#include "monger/db/client.h"
#include "monger/db/db_raii.h"
#include "monger/db/dbdirectclient.h"
#include "monger/db/dbhelpers.h"
#include "monger/db/jsobj.h"
#include "monger/db/operation_context.h"
#include "monger/db/service_context.h"
#include "monger/db/storage/storage_engine.h"
#include "monger/util/assert_util.h"
#include "monger/util/log.h"
#include "monger/util/str.h"

namespace monger {

AuthzManagerExternalStateMongerd::AuthzManagerExternalStateMongerd() = default;
AuthzManagerExternalStateMongerd::~AuthzManagerExternalStateMongerd() = default;

std::unique_ptr<AuthzSessionExternalState>
AuthzManagerExternalStateMongerd::makeAuthzSessionExternalState(AuthorizationManager* authzManager) {
    return std::make_unique<AuthzSessionExternalStateMongerd>(authzManager);
}
Status AuthzManagerExternalStateMongerd::query(
    OperationContext* opCtx,
    const NamespaceString& collectionName,
    const BSONObj& query,
    const BSONObj& projection,
    const std::function<void(const BSONObj&)>& resultProcessor) {
    try {
        DBDirectClient client(opCtx);
        client.query(resultProcessor, collectionName, query, &projection);
        return Status::OK();
    } catch (const DBException& e) {
        return e.toStatus();
    }
}

Status AuthzManagerExternalStateMongerd::findOne(OperationContext* opCtx,
                                                const NamespaceString& collectionName,
                                                const BSONObj& query,
                                                BSONObj* result) {
    AutoGetCollectionForReadCommand ctx(opCtx, collectionName);

    BSONObj found;
    if (Helpers::findOne(opCtx, ctx.getCollection(), query, found)) {
        *result = found.getOwned();
        return Status::OK();
    }
    return Status(ErrorCodes::NoMatchingDocument,
                  str::stream() << "No document in " << collectionName.ns() << " matches "
                                << query);
}

MONGO_REGISTER_SHIM(AuthzManagerExternalState::create)
()->std::unique_ptr<AuthzManagerExternalState> {
    return std::make_unique<AuthzManagerExternalStateMongerd>();
}

}  // namespace monger
