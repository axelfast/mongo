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

namespace {

KillAllSessionsByPatternSet patternsForLoggedInUser(OperationContext* opCtx) {
    auto client = opCtx->getClient();
    ServiceContext* serviceContext = client->getServiceContext();

    KillAllSessionsByPatternSet patterns;

    if (AuthorizationManager::get(serviceContext)->isAuthEnabled()) {
        auto authzSession = AuthorizationSession::get(client);
        for (auto iter = authzSession->getAuthenticatedUserNames(); iter.more(); iter.next()) {
            User* user = authzSession->lookupUser(*iter);
            invariant(user);

            auto pattern = makeKillAllSessionsByPattern(opCtx);
            pattern.setUid(user->getDigest());
            patterns.emplace(std::move(pattern));
        }
    } else {
        patterns.emplace(makeKillAllSessionsByPattern(opCtx));
    }

    return patterns;
}

}  // namespace

class KillSessionsCommand final : public BasicCommand {
    KillSessionsCommand(const KillSessionsCommand&) = delete;
    KillSessionsCommand& operator=(const KillSessionsCommand&) = delete;

public:
    KillSessionsCommand() : BasicCommand("killSessions") {}

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
        return "kill a logical session and its operations";
    }

    // Any user can kill their own sessions
    Status checkAuthForOperation(OperationContext* opCtx,
                                 const std::string& dbname,
                                 const BSONObj& cmdObj) const override {
        return Status::OK();
    }

    virtual bool run(OperationContext* opCtx,
                     const std::string& db,
                     const BSONObj& cmdObj,
                     BSONObjBuilder& result) override {
        IDLParserErrorContext ctx("KillSessionsCmd");
        auto ksc = KillSessionsCmdFromClient::parse(ctx, cmdObj);

        KillAllSessionsByPatternSet patterns;

        if (ksc.getKillSessions().empty()) {
            patterns = patternsForLoggedInUser(opCtx);
        } else {
            auto lsids = makeLogicalSessionIds(
                ksc.getKillSessions(),
                opCtx,
                {Privilege{ResourcePattern::forClusterResource(), ActionType::killAnySession}});

            patterns.reserve(lsids.size());
            for (const auto& lsid : lsids) {
                patterns.emplace(makeKillAllSessionsByPattern(opCtx, lsid));
            }
        }

        uassertStatusOK(killSessionsCmdHelper(opCtx, result, patterns));
        return true;
    }
} killSessionsCommand;

}  // namespace monger
