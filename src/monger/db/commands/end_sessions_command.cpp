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

#include "monger/base/init.h"
#include "monger/db/auth/authorization_session.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/commands/sessions_commands_gen.h"
#include "monger/db/logical_session_cache.h"
#include "monger/db/logical_session_id_helpers.h"
#include "monger/db/operation_context.h"

namespace monger {
namespace {

class EndSessionsCommand final : public BasicCommand {
    EndSessionsCommand(const EndSessionsCommand&) = delete;
    EndSessionsCommand& operator=(const EndSessionsCommand&) = delete;

public:
    EndSessionsCommand() : BasicCommand("endSessions") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }

    bool adminOnly() const override {
        return false;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }

    std::string help() const override {
        return "end a set of logical sessions";
    }

    Status checkAuthForOperation(OperationContext* opCtx,
                                 const std::string& dbname,
                                 const BSONObj& cmdObj) const override {
        // It is always ok to run this command, as long as you are authenticated
        // as some user, if auth is enabled.
        AuthorizationSession* authSession = AuthorizationSession::get(opCtx->getClient());
        try {
            auto user = authSession->getSingleUser();
            invariant(user);
            return Status::OK();
        } catch (...) {
            return exceptionToStatus();
        }
    }

    bool run(OperationContext* opCtx,
             const std::string& db,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) override {
        auto endSessionsRequest = EndSessionsCmdFromClient::parse(
            IDLParserErrorContext("EndSessionsCmdFromClient"), cmdObj);

        LogicalSessionCache::get(opCtx)->endSessions(
            makeLogicalSessionIds(endSessionsRequest.getSessions(), opCtx));

        return true;
    }

} endSessionsCommand;

}  // namespace
}  // namespace monger
