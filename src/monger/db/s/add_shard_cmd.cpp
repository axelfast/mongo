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
#include "monger/db/dbdirectclient.h"
#include "monger/db/s/add_shard_cmd_gen.h"
#include "monger/db/s/add_shard_util.h"
#include "monger/db/s/config/sharding_catalog_manager.h"
#include "monger/rpc/get_status_from_command_result.h"
#include "monger/s/balancer_configuration.h"
#include "monger/s/grid.h"
#include "monger/util/log.h"

namespace monger {
namespace {
/**
 * Internal sharding command run on mongerd to initialize itself as a shard in the cluster.
 */
class AddShardCommand : public TypedCommand<AddShardCommand> {
public:
    using Request = AddShard;
    class Invocation final : public InvocationBase {
    public:
        using InvocationBase::InvocationBase;

        void typedRun(OperationContext* opCtx) {
            uassert(50876,
                    "Cannot run addShard on a node started without --shardsvr",
                    serverGlobalParams.clusterRole == ClusterRole::ShardServer);

            auto addShardCmd = request();
            auto shardIdUpsertCmd =
                add_shard_util::createShardIdentityUpsertForAddShard(addShardCmd);
            DBDirectClient localClient(opCtx);
            BSONObj res;

            localClient.runCommand(NamespaceString::kAdminDb.toString(), shardIdUpsertCmd, res);

            uassertStatusOK(getStatusFromCommandResult(res));

            const auto balancerConfig = Grid::get(opCtx)->getBalancerConfiguration();
            invariant(balancerConfig);
            // Ensure we have the most up-to-date balancer configuration
            uassertStatusOK(balancerConfig->refreshAndCheck(opCtx));
        }

    private:
        bool supportsWriteConcern() const override {
            return true;
        }

        // The command parameter happens to be string so it's historically been interpreted
        // by parseNs as a collection. Continuing to do so here for unexamined compatibility.
        NamespaceString ns() const override {
            return NamespaceString(request().getDbName(), "");
        }

        void doCheckAuthorization(OperationContext* opCtx) const override {
            uassert(ErrorCodes::Unauthorized,
                    "Unauthorized",
                    AuthorizationSession::get(opCtx->getClient())
                        ->isAuthorizedForActionsOnResource(ResourcePattern::forClusterResource(),
                                                           ActionType::internal));
        }
    };

    std::string help() const override {
        return "Internal command, which is exported by shards. Do not call "
               "directly. Adds a new shard to a cluster.";
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }

    bool adminOnly() const override {
        return true;
    }
} addShardCmd;

}  // namespace
}  // namespace monger
