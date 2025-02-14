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

#include "monger/db/auth/action_set.h"
#include "monger/db/auth/authorization_session.h"
#include "monger/db/auth/resource_pattern.h"
#include "monger/db/catalog/document_validation.h"
#include "monger/db/cloner.h"
#include "monger/db/commands.h"
#include "monger/db/dbdirectclient.h"
#include "monger/db/s/config/sharding_catalog_manager.h"
#include "monger/db/s/sharding_state.h"
#include "monger/rpc/get_status_from_command_result.h"
#include "monger/s/grid.h"
#include "monger/s/request_types/clone_catalog_data_gen.h"
#include "monger/util/log.h"

namespace monger {
namespace {

/**
 * Currently, _cloneCatalogData will clone all data (including metadata). In the second part of
 * PM-1017 (Introduce Database Versioning in Sharding Config) this command will be changed to only
 * clone catalog metadata, as the name would suggest.
 */
class CloneCatalogDataCommand : public BasicCommand {
public:
    CloneCatalogDataCommand() : BasicCommand("_cloneCatalogData") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }

    virtual bool adminOnly() const {
        return true;
    }

    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
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
             const std::string& dbname_unused,
             const BSONObj& cmdObj,
             BSONObjBuilder& result) override {

        auto shardingState = ShardingState::get(opCtx);
        uassertStatusOK(shardingState->canAcceptShardedCommands());

        uassert(ErrorCodes::IllegalOperation,
                str::stream() << "_cloneCatalogData can only be run on shard servers",
                serverGlobalParams.clusterRole == ClusterRole::ShardServer);

        uassert(ErrorCodes::InvalidOptions,
                str::stream() << "_cloneCatalogData must be called with majority writeConcern, got "
                              << cmdObj,
                opCtx->getWriteConcern().wMode == WriteConcernOptions::kMajority);

        const auto cloneCatalogDataRequest =
            CloneCatalogData::parse(IDLParserErrorContext("_cloneCatalogData"), cmdObj);
        const auto dbname = cloneCatalogDataRequest.getCommandParameter().toString();

        uassert(
            ErrorCodes::InvalidNamespace,
            str::stream() << "invalid db name specified: " << dbname,
            NamespaceString::validDBName(dbname, NamespaceString::DollarInDbNameBehavior::Allow));

        uassert(ErrorCodes::InvalidOptions,
                str::stream() << "Can't clone catalog data for " << dbname << " database",
                dbname != NamespaceString::kAdminDb && dbname != NamespaceString::kConfigDb &&
                    dbname != NamespaceString::kLocalDb);

        auto from = cloneCatalogDataRequest.getFrom();

        uassert(ErrorCodes::InvalidOptions,
                str::stream() << "Can't run _cloneCatalogData without a source",
                !from.empty());

        auto const catalogClient = Grid::get(opCtx)->catalogClient();
        const auto shardedColls = catalogClient->getAllShardedCollectionsForDb(
            opCtx, dbname, repl::ReadConcernLevel::kMajorityReadConcern);

        CloneOptions opts;
        opts.fromDB = dbname;
        for (const auto& shardedColl : shardedColls) {
            opts.shardedColls.insert(shardedColl.ns());
        }

        DisableDocumentValidation disableValidation(opCtx);

        // Clone the non-ignored collections.
        std::set<std::string> clonedColls;
        Lock::DBLock dbXLock(opCtx, dbname, MODE_X);

        Cloner cloner;
        uassertStatusOK(cloner.copyDb(opCtx, dbname, from.toString(), opts, &clonedColls));
        {
            BSONArrayBuilder cloneBarr = result.subarrayStart("clonedColls");
            cloneBarr.append(clonedColls);
        }

        return true;
    }

} CloneCatalogDataCmd;

}  // namespace
}  // namespace monger
