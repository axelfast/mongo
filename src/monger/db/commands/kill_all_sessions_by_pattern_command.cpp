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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kCommand

#include "monger/platform/basic.h"

#include "monger/base/init.h"
#include "monger/db/auth/action_set.h"
#include "monger/db/auth/action_type.h"
#include "monger/db/auth/authorization_manager.h"
#include "monger/db/auth/authorization_session.h"
#include "monger/db/auth/privilege.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/jsobj.h"
#include "monger/db/kill_sessions.h"
#include "monger/db/kill_sessions_common.h"
#include "monger/db/kill_sessions_local.h"
#include "monger/db/logical_session_cache.h"
#include "monger/db/logical_session_id.h"
#include "monger/db/logical_session_id_helpers.h"
#include "monger/db/operation_context.h"
#include "monger/db/stats/top.h"
#include "monger/util/log.h"

namespace monger {

class KillAllSessionsByPatternCommand final : public BasicCommand {
    KillAllSessionsByPatternCommand(const KillAllSessionsByPatternCommand&) = delete;
    KillAllSessionsByPatternCommand& operator=(const KillAllSessionsByPatternCommand&) = delete;

public:
    KillAllSessionsByPatternCommand() : BasicCommand("killAllSessionsByPattern") {}

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
        return "kill logical sessions by pattern";
    }
    Status checkAuthForOperation(OperationContext* opCtx,
                                 const std::string& dbname,
                                 const BSONObj& cmdObj) const override {
        AuthorizationSession* authSession = AuthorizationSession::get(opCtx->getClient());
        if (!authSession->isAuthorizedForPrivilege(
                Privilege{ResourcePattern::forClusterResource(), ActionType::killAnySession})) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }

        return Status::OK();
    }

    virtual bool run(OperationContext* opCtx,
                     const std::string& db,
                     const BSONObj& cmdObj,
                     BSONObjBuilder& result) override {
        IDLParserErrorContext ctx("KillAllSessionsByPatternCmd");
        auto ksc = KillAllSessionsByPatternCmd::parse(ctx, cmdObj);

        // The empty command kills all
        if (ksc.getKillAllSessionsByPattern().empty()) {
            ksc.setKillAllSessionsByPattern({makeKillAllSessionsByPattern(opCtx)});
        } else {
            // If a pattern is passed, you may only pass impersonate data if you have the
            // impersonate privilege.
            auto authSession = AuthorizationSession::get(opCtx->getClient());

            if (!authSession->isAuthorizedForPrivilege(
                    Privilege(ResourcePattern::forClusterResource(), ActionType::impersonate))) {

                for (const auto& pattern : ksc.getKillAllSessionsByPattern()) {
                    if (pattern.getUsers() || pattern.getRoles()) {
                        uasserted(ErrorCodes::Unauthorized,
                                  "Not authorized to impersonate in killAllSessionsByPattern");
                    }
                }
            }
        }

        KillAllSessionsByPatternSet patterns{ksc.getKillAllSessionsByPattern().begin(),
                                             ksc.getKillAllSessionsByPattern().end()};

        uassertStatusOK(killSessionsCmdHelper(opCtx, result, patterns));
        return true;
    }
} killAllSessionsByPatternCommand;

}  // namespace monger
