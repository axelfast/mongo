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

#include "monger/db/auth/action_set.h"
#include "monger/db/auth/action_type.h"
#include "monger/db/auth/authorization_session.h"
#include "monger/db/auth/privilege.h"
#include "monger/db/catalog_raii.h"
#include "monger/db/commands.h"
#include "monger/db/s/database_sharding_state.h"
#include "monger/s/request_types/get_database_version_gen.h"
#include "monger/util/str.h"

namespace monger {
namespace {

class GetDatabaseVersionCmd final : public TypedCommand<GetDatabaseVersionCmd> {
public:
    using Request = GetDatabaseVersion;

    class Invocation final : public MinimalInvocationBase {
    public:
        using MinimalInvocationBase::MinimalInvocationBase;

    private:
        bool supportsWriteConcern() const override {
            return false;
        }

        // The command parameter happens to be string so it's historically been interpreted
        // by parseNs as a collection. Continuing to do so here for unexamined compatibility.
        NamespaceString ns() const override {
            return NamespaceString(request().getDbName(), _targetDb());
        }

        void doCheckAuthorization(OperationContext* opCtx) const override {
            uassert(ErrorCodes::Unauthorized,
                    "Unauthorized",
                    AuthorizationSession::get(opCtx->getClient())
                        ->isAuthorizedForActionsOnResource(
                            ResourcePattern::forDatabaseName(_targetDb()),
                            ActionType::getDatabaseVersion));
        }

        void run(OperationContext* opCtx, rpc::ReplyBuilderInterface* result) override {
            uassert(ErrorCodes::IllegalOperation,
                    str::stream() << definition()->getName() << " can only be run on shard servers",
                    serverGlobalParams.clusterRole == ClusterRole::ShardServer);
            BSONObj versionObj;
            AutoGetDb autoDb(opCtx, _targetDb(), MODE_IS);
            if (auto db = autoDb.getDb()) {
                auto& dss = DatabaseShardingState::get(db);
                auto dssLock = DatabaseShardingState::DSSLock::lockShared(opCtx, &dss);

                if (auto dbVersion = dss.getDbVersion(opCtx, dssLock)) {
                    versionObj = dbVersion->toBSON();
                }
            }
            result->getBodyBuilder().append("dbVersion", versionObj);
        }

        StringData _targetDb() const {
            return request().getCommandParameter();
        }
    };

    std::string help() const override {
        return " example: { getDatabaseVersion : 'foo'  } ";
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }

    bool adminOnly() const override {
        return true;
    }
} getDatabaseVersionCmd;

}  // namespace
}  // namespace monger
