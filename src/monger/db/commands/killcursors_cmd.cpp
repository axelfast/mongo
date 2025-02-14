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
#include "monger/db/catalog/collection.h"
#include "monger/db/commands/killcursors_common.h"
#include "monger/db/curop.h"
#include "monger/db/cursor_manager.h"
#include "monger/db/db_raii.h"
#include "monger/db/query/killcursors_request.h"
#include "monger/db/stats/top.h"
#include "monger/util/scopeguard.h"

namespace monger {

class KillCursorsCmd final : public KillCursorsCmdBase {
    KillCursorsCmd(const KillCursorsCmd&) = delete;
    KillCursorsCmd& operator=(const KillCursorsCmd&) = delete;

public:
    KillCursorsCmd() = default;

    bool supportsReadConcern(const std::string& dbName,
                             const BSONObj& cmdObj,
                             repl::ReadConcernLevel level) const final {
        // killCursors must support snapshot read concern in order to be run in transactions.
        return level == repl::ReadConcernLevel::kLocalReadConcern ||
            level == repl::ReadConcernLevel::kSnapshotReadConcern;
    }

    bool run(OperationContext* opCtx,
             const std::string& dbname,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) final {
        return runImpl(opCtx, dbname, cmdObj, result);
    }

private:
    Status _checkAuth(Client* client, const NamespaceString& nss, CursorId id) const final {
        auto opCtx = client->getOperationContext();
        return CursorManager::get(opCtx)->checkAuthForKillCursors(opCtx, id);
    }

    Status _killCursor(OperationContext* opCtx,
                       const NamespaceString& nss,
                       CursorId id) const final {
        boost::optional<AutoStatsTracker> statsTracker;
        if (!nss.isCollectionlessCursorNamespace()) {
            const boost::optional<int> dbProfilingLevel = boost::none;
            statsTracker.emplace(opCtx,
                                 nss,
                                 Top::LockType::NotLocked,
                                 AutoStatsTracker::LogMode::kUpdateTopAndCurop,
                                 dbProfilingLevel);
        }

        auto cursorManager = CursorManager::get(opCtx);
        return cursorManager->killCursor(opCtx, id, true /* shouldAudit */);
    }
} killCursorsCmd;

}  // namespace monger
