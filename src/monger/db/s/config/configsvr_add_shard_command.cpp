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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kSharding

#include "monger/platform/basic.h"

#include "monger/db/audit.h"
#include "monger/db/auth/action_type.h"
#include "monger/db/auth/authorization_session.h"
#include "monger/db/auth/privilege.h"
#include "monger/db/commands.h"
#include "monger/db/commands/feature_compatibility_version.h"
#include "monger/db/namespace_string.h"
#include "monger/db/repl/read_concern_args.h"
#include "monger/db/repl/repl_set_config.h"
#include "monger/db/repl/replication_coordinator.h"
#include "monger/db/s/config/sharding_catalog_manager.h"
#include "monger/s/catalog/type_shard.h"
#include "monger/s/grid.h"
#include "monger/s/request_types/add_shard_request_type.h"
#include "monger/util/log.h"
#include "monger/util/str.h"

namespace monger {
namespace {

using std::string;

const long long kMaxSizeMBDefault = 0;

/**
 * Internal sharding command run on config servers to add a shard to the cluster.
 */
class ConfigSvrAddShardCommand : public BasicCommand {
public:
    ConfigSvrAddShardCommand() : BasicCommand("_configsvrAddShard") {}

    std::string help() const override {
        return "Internal command, which is exported by the sharding config server. Do not call "
               "directly. Validates and adds a new shard to the cluster.";
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }

    bool adminOnly() const override {
        return true;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }

    Status checkAuthForCommand(Client* client,
                               const std::string& dbname,
                               const BSONObj& cmdObj) const override {
        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forClusterResource(), ActionType::internal)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }
        return Status::OK();
    }

    bool run(OperationContext* opCtx,
             const std::string& unusedDbName,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) override {
        uassert(ErrorCodes::IllegalOperation,
                "_configsvrAddShard can only be run on config servers",
                serverGlobalParams.clusterRole == ClusterRole::ConfigServer);

        // Set the operation context read concern level to local for reads into the config database.
        repl::ReadConcernArgs::get(opCtx) =
            repl::ReadConcernArgs(repl::ReadConcernLevel::kLocalReadConcern);

        auto swParsedRequest = AddShardRequest::parseFromConfigCommand(cmdObj);
        uassertStatusOK(swParsedRequest.getStatus());
        auto parsedRequest = std::move(swParsedRequest.getValue());

        auto replCoord = repl::ReplicationCoordinator::get(opCtx);
        auto rsConfig = replCoord->getConfig();

        auto validationStatus = parsedRequest.validate(rsConfig.isLocalHostAllowed());
        uassertStatusOK(validationStatus);

        uassert(ErrorCodes::InvalidOptions,
                str::stream() << "addShard must be called with majority writeConcern, got "
                              << cmdObj,
                opCtx->getWriteConcern().wMode == WriteConcernOptions::kMajority);

        audit::logAddShard(Client::getCurrent(),
                           parsedRequest.hasName() ? parsedRequest.getName() : "",
                           parsedRequest.getConnString().toString(),
                           parsedRequest.hasMaxSize() ? parsedRequest.getMaxSize()
                                                      : kMaxSizeMBDefault);

        StatusWith<string> addShardResult = ShardingCatalogManager::get(opCtx)->addShard(
            opCtx,
            parsedRequest.hasName() ? &parsedRequest.getName() : nullptr,
            parsedRequest.getConnString(),
            parsedRequest.hasMaxSize() ? parsedRequest.getMaxSize() : kMaxSizeMBDefault);

        if (!addShardResult.isOK()) {
            log() << "addShard request '" << parsedRequest << "'"
                  << "failed" << causedBy(addShardResult.getStatus());
            uassertStatusOK(addShardResult.getStatus());
        }

        result << "shardAdded" << addShardResult.getValue();

        return true;
    }
} configsvrAddShardCmd;

}  // namespace
}  // namespace monger
