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

#include <list>
#include <set>
#include <vector>

#include "monger/bson/simple_bsonelement_comparator.h"
#include "monger/bson/simple_bsonobj_comparator.h"
#include "monger/bson/util/bson_extract.h"
#include "monger/client/connpool.h"
#include "monger/db/audit.h"
#include "monger/db/auth/action_set.h"
#include "monger/db/auth/action_type.h"
#include "monger/db/auth/authorization_session.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/hasher.h"
#include "monger/db/index/index_descriptor.h"
#include "monger/db/operation_context.h"
#include "monger/db/query/collation/collator_factory_interface.h"
#include "monger/db/write_concern_options.h"
#include "monger/s/balancer_configuration.h"
#include "monger/s/catalog/sharding_catalog_client.h"
#include "monger/s/catalog_cache.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/cluster_commands_helpers.h"
#include "monger/s/config_server_client.h"
#include "monger/s/grid.h"
#include "monger/s/request_types/migration_secondary_throttle_options.h"
#include "monger/s/request_types/shard_collection_gen.h"
#include "monger/util/log.h"
#include "monger/util/scopeguard.h"

namespace monger {
namespace {

class ShardCollectionCmd : public BasicCommand {
public:
    ShardCollectionCmd() : BasicCommand("shardCollection", "shardcollection") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }

    bool adminOnly() const override {
        return true;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }

    std::string help() const override {
        return "Shard a collection. Requires key. Optional unique."
               " Sharding must already be enabled for the database.\n"
               "   { enablesharding : \"<dbname>\" }\n";
    }

    Status checkAuthForCommand(Client* client,
                               const std::string& dbname,
                               const BSONObj& cmdObj) const override {
        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forExactNamespace(NamespaceString(parseNs(dbname, cmdObj))),
                ActionType::enableSharding)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }

        return Status::OK();
    }

    std::string parseNs(const std::string& dbname, const BSONObj& cmdObj) const override {
        return CommandHelpers::parseNsFullyQualified(cmdObj);
    }

    bool run(OperationContext* opCtx,
             const std::string& dbname,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) override {
        const NamespaceString nss(parseNs(dbname, cmdObj));
        auto shardCollRequest =
            ShardCollection::parse(IDLParserErrorContext("ShardCollection"), cmdObj);

        ConfigsvrShardCollectionRequest configShardCollRequest;
        configShardCollRequest.set_configsvrShardCollection(nss);
        configShardCollRequest.setKey(shardCollRequest.getKey());
        configShardCollRequest.setUnique(shardCollRequest.getUnique());
        configShardCollRequest.setNumInitialChunks(shardCollRequest.getNumInitialChunks());
        configShardCollRequest.setCollation(shardCollRequest.getCollation());

        // Invalidate the routing table cache entry for this collection so that we reload the
        // collection the next time it's accessed, even if we receive a failure, e.g. NetworkError.
        ON_BLOCK_EXIT(
            [opCtx, nss] { Grid::get(opCtx)->catalogCache()->invalidateShardedCollection(nss); });

        auto configShard = Grid::get(opCtx)->shardRegistry()->getConfigShard();
        auto cmdResponse = uassertStatusOK(configShard->runCommandWithFixedRetryAttempts(
            opCtx,
            ReadPreferenceSetting(ReadPreference::PrimaryOnly),
            "admin",
            CommandHelpers::appendMajorityWriteConcern(
                CommandHelpers::appendPassthroughFields(cmdObj, configShardCollRequest.toBSON())),
            Shard::RetryPolicy::kIdempotent));

        CommandHelpers::filterCommandReplyForPassthrough(cmdResponse.response, &result);
        return true;
    }

} shardCollectionCmd;

}  // namespace
}  // namespace monger
