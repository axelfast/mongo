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

#include "monger/db/commands/current_op_common.h"

#include "monger/db/auth/action_type.h"
#include "monger/db/auth/authorization_session.h"
#include "monger/db/client.h"
#include "monger/db/commands/fsync_locked.h"
#include "monger/db/commands/run_aggregate.h"
#include "monger/db/pipeline/document.h"
#include "monger/db/stats/fill_locker_info.h"

namespace monger {

class CurrentOpCommand final : public CurrentOpCommandBase {
    CurrentOpCommand(const CurrentOpCommand&) = delete;
    CurrentOpCommand& operator=(const CurrentOpCommand&) = delete;

public:
    CurrentOpCommand() = default;

    Status checkAuthForCommand(Client* client,
                               const std::string& dbName,
                               const BSONObj& cmdObj) const final {
        AuthorizationSession* authzSession = AuthorizationSession::get(client);
        if (authzSession->isAuthorizedForActionsOnResource(ResourcePattern::forClusterResource(),
                                                           ActionType::inprog)) {
            return Status::OK();
        }

        if (authzSession->isAuthenticated() && cmdObj["$ownOps"].trueValue()) {
            return Status::OK();
        }

        return Status(ErrorCodes::Unauthorized, "Unauthorized");
    }

    virtual StatusWith<CursorResponse> runAggregation(
        OperationContext* opCtx, const AggregationRequest& request) const final {
        auto aggCmdObj = request.serializeToCommandObj().toBson();

        rpc::OpMsgReplyBuilder replyBuilder;

        PrivilegeVector privileges;
        if (!aggCmdObj["$ownOps"].trueValue()) {
            privileges = {Privilege(ResourcePattern::forClusterResource(), ActionType::inprog)};
        }

        auto status = runAggregate(opCtx,
                                   request.getNamespaceString(),
                                   request,
                                   std::move(aggCmdObj),
                                   privileges,
                                   &replyBuilder);

        if (!status.isOK()) {
            return status;
        }

        auto bodyBuilder = replyBuilder.getBodyBuilder();
        CommandHelpers::appendSimpleCommandStatus(bodyBuilder, true);
        bodyBuilder.doneFast();

        return CursorResponse::parseFromBSON(replyBuilder.releaseBody());
    }

    virtual void appendToResponse(BSONObjBuilder* result) const final {
        if (lockedForWriting()) {
            result->append("fsyncLock", true);
            result->append("info",
                           "use db.fsyncUnlock() to terminate the fsync write/snapshot lock");
        }
    }

} currentOpCommand;

}  // namespace monger
