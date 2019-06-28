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

#include "monger/db/s/add_shard_util.h"

#include "monger/bson/bsonobj.h"
#include "monger/db/operation_context.h"
#include "monger/db/ops/write_ops.h"
#include "monger/db/repl/repl_set_config.h"
#include "monger/db/repl/replication_coordinator.h"
#include "monger/db/s/add_shard_cmd_gen.h"
#include "monger/s/catalog/sharding_catalog_client.h"
#include "monger/s/cluster_identity_loader.h"
#include "monger/s/shard_id.h"
#include "monger/s/write_ops/batched_command_request.h"

namespace monger {
namespace add_shard_util {

AddShard createAddShardCmd(OperationContext* opCtx, const ShardId& shardName) {
    AddShard addShardCmd;
    addShardCmd.setDbName(NamespaceString::kAdminDb);

    ShardIdentity shardIdentity;
    shardIdentity.setShardName(shardName.toString());
    shardIdentity.setClusterId(ClusterIdentityLoader::get(opCtx)->getClusterId());
    shardIdentity.setConfigsvrConnectionString(
        repl::ReplicationCoordinator::get(opCtx)->getConfig().getConnectionString());

    addShardCmd.setShardIdentity(shardIdentity);
    return addShardCmd;
}

BSONObj createShardIdentityUpsertForAddShard(const AddShard& addShardCmd) {
    BatchedCommandRequest request([&] {
        write_ops::Update updateOp(NamespaceString::kServerConfigurationNamespace);
        updateOp.setUpdates({[&] {
            write_ops::UpdateOpEntry entry;
            entry.setQ(BSON("_id" << kShardIdentityDocumentId));
            entry.setU(addShardCmd.getShardIdentity().toBSON());
            entry.setUpsert(true);
            return entry;
        }()});

        return updateOp;
    }());
    request.setWriteConcern(ShardingCatalogClient::kMajorityWriteConcern.toBSON());

    return request.toBSON();
}

}  // namespace monger
}  // namespace add_shard_util
