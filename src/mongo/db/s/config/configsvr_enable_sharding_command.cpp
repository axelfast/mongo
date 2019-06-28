/**
 *    Copyright (C) 2018-present MongerDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongerDB, Inc.
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

#include <set>

#include "monger/db/audit.h"
#include "monger/db/auth/action_set.h"
#include "monger/db/auth/action_type.h"
#include "monger/db/auth/authorization_session.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/operation_context.h"
#include "monger/db/repl/read_concern_args.h"
#include "monger/db/s/config/sharding_catalog_manager.h"
#include "monger/s/catalog/type_database.h"
#include "monger/s/catalog_cache.h"
#include "monger/s/grid.h"
#include "monger/util/log.h"
#include "monger/util/scopeguard.h"

namespace monger {

using std::shared_ptr;
using std::set;
using std::string;

namespace {

/**
 * Internal sharding command run on config servers to enable sharding on a database.
 */
class ConfigSvrEnableShardingCommand : public BasicCommand {
public:
    ConfigSvrEnableShardingCommand() : BasicCommand("_configsvrEnableSharding") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }

    virtual bool adminOnly() const {
        return true;
    }

    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }

    std::string help() const override {
        return "Internal command, which is exported by the sharding config server. Do not call "
               "directly. Enable sharding on a database.";
    }

    virtual Status checkAuthForCommand(Client* client,
                                       const std::string& dbname,
                                       const BSONObj& cmdObj) const override {
        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forDatabaseName(parseNs(dbname, cmdObj)), ActionType::internal)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }

        return Status::OK();
    }

    virtual std::string parseNs(const std::string& dbname, const BSONObj& cmdObj) const {
        return cmdObj.firstElement().str();
    }

    bool run(OperationContext* opCtx,
             const std::string& dbname_unused,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) {
        uassert(ErrorCodes::IllegalOperation,
                "_configsvrEnableSharding can only be run on config servers",
                serverGlobalParams.clusterRole == ClusterRole::ConfigServer);

        // Set the operation context read concern level to local for reads into the config database.
        repl::ReadConcernArgs::get(opCtx) =
            repl::ReadConcernArgs(repl::ReadConcernLevel::kLocalReadConcern);

        const std::string dbname = parseNs("", cmdObj);

        uassert(
            ErrorCodes::InvalidNamespace,
            str::stream() << "invalid db name specified: " << dbname,
            NamespaceString::validDBName(dbname, NamespaceString::DollarInDbNameBehavior::Allow));

        if (dbname == NamespaceString::kAdminDb || dbname == NamespaceString::kLocalDb) {
            uasserted(ErrorCodes::InvalidOptions,
                      str::stream() << "can't shard " + dbname + " database");
        }

        uassert(ErrorCodes::InvalidOptions,
                str::stream() << "enableSharding must be called with majority writeConcern, got "
                              << cmdObj,
                opCtx->getWriteConcern().wMode == WriteConcernOptions::kMajority);

        // Make sure to force update of any stale metadata
        ON_BLOCK_EXIT([opCtx, dbname] { Grid::get(opCtx)->catalogCache()->purgeDatabase(dbname); });

        auto dbDistLock =
            uassertStatusOK(Grid::get(opCtx)->catalogClient()->getDistLockManager()->lock(
                opCtx, dbname, "enableSharding", DistLockManager::kDefaultLockTimeout));

        ShardingCatalogManager::get(opCtx)->enableSharding(opCtx, dbname);
        audit::logEnableSharding(Client::getCurrent(), dbname);

        return true;
    }

} configsvrEnableShardingCmd;

}  // namespace
}  // namespace monger
