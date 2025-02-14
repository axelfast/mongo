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
#include "monger/db/commands.h"
#include "monger/db/commands/feature_compatibility_version_command_parser.h"
#include "monger/db/commands/feature_compatibility_version_documentation.h"
#include "monger/db/commands/feature_compatibility_version_parser.h"
#include "monger/s/client/shard.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/grid.h"
#include "monger/util/str.h"

namespace monger {

namespace {

/**
 * Sets the minimum allowed version for the cluster. If it is the last stable
 * featureCompatibilityVersion, then shards will not use latest featureCompatibilityVersion
 * features.
 *
 * Format:
 * {
 *   setFeatureCompatibilityVersion: <string version>
 * }
 */
class SetFeatureCompatibilityVersionCmd : public BasicCommand {
public:
    SetFeatureCompatibilityVersionCmd() : BasicCommand("setFeatureCompatibilityVersion") {}

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
        return str::stream()
            << "Set the API version for the cluster. If set to \""
            << FeatureCompatibilityVersionParser::kVersion40
            << "\", then 4.2 features are disabled. If \""
            << FeatureCompatibilityVersionParser::kVersion42
            << "\", then 4.2 features are enabled, and all nodes in the cluster must be version "
            << "4.2. See " << feature_compatibility_version_documentation::kCompatibilityLink
            << ".";
    }

    Status checkAuthForCommand(Client* client,
                               const std::string& dbname,
                               const BSONObj& cmdObj) const override {
        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forClusterResource(),
                ActionType::setFeatureCompatibilityVersion)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }
        return Status::OK();
    }

    bool run(OperationContext* opCtx,
             const std::string& dbname,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) {
        const auto version = uassertStatusOK(
            FeatureCompatibilityVersionCommandParser::extractVersionFromCommand(getName(), cmdObj));

        // Forward to config shard, which will forward to all shards.
        auto configShard = Grid::get(opCtx)->shardRegistry()->getConfigShard();
        auto response = uassertStatusOK(configShard->runCommandWithFixedRetryAttempts(
            opCtx,
            ReadPreferenceSetting{ReadPreference::PrimaryOnly},
            dbname,
            CommandHelpers::appendMajorityWriteConcern(CommandHelpers::appendPassthroughFields(
                cmdObj, BSON("setFeatureCompatibilityVersion" << version))),
            Shard::RetryPolicy::kIdempotent));
        uassertStatusOK(response.commandStatus);

        return true;
    }

} clusterSetFeatureCompatibilityVersionCmd;

}  // namespace
}  // namespace monger
