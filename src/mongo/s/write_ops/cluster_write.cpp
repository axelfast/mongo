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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kSharding

#include "monger/platform/basic.h"

#include "monger/s/write_ops/cluster_write.h"

#include <algorithm>

#include "monger/base/status.h"
#include "monger/client/connpool.h"
#include "monger/client/dbclient_cursor.h"
#include "monger/db/lasterror.h"
#include "monger/db/write_concern_options.h"
#include "monger/s/balancer_configuration.h"
#include "monger/s/catalog/type_collection.h"
#include "monger/s/catalog_cache.h"
#include "monger/s/chunk_writes_tracker.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/config_server_client.h"
#include "monger/s/grid.h"
#include "monger/s/shard_util.h"
#include "monger/s/write_ops/chunk_manager_targeter.h"
#include "monger/util/log.h"
#include "monger/util/str.h"

namespace monger {
namespace {

void toBatchError(const Status& status, BatchedCommandResponse* response) {
    response->clear();
    response->setStatus(status);
    dassert(response->isValid(nullptr));
}

}  // namespace

void ClusterWriter::write(OperationContext* opCtx,
                          const BatchedCommandRequest& request,
                          BatchWriteExecStats* stats,
                          BatchedCommandResponse* response,
                          boost::optional<OID> targetEpoch) {
    const NamespaceString& nss = request.getNS();

    LastError::Disabled disableLastError(&LastError::get(opCtx->getClient()));

    // Config writes and shard writes are done differently
    if (nss.db() == NamespaceString::kAdminDb) {
        Grid::get(opCtx)->catalogClient()->writeConfigServerDirect(opCtx, request, response);
    } else {
        {
            ChunkManagerTargeter targeter(request.getNS(), targetEpoch);

            Status targetInitStatus = targeter.init(opCtx);
            if (!targetInitStatus.isOK()) {
                toBatchError(targetInitStatus.withContext(
                                 str::stream()
                                 << "unable to initialize targeter for write op for collection "
                                 << request.getNS().ns()),
                             response);
                return;
            }

            auto swEndpoints = targeter.targetCollection();
            if (!swEndpoints.isOK()) {
                toBatchError(swEndpoints.getStatus().withContext(
                                 str::stream() << "unable to target write op for collection "
                                               << request.getNS().ns()),
                             response);
                return;
            }

            const auto& endpoints = swEndpoints.getValue();

            // Handle sharded config server writes differently.
            if (std::any_of(endpoints.begin(), endpoints.end(), [](const auto& it) {
                    return it.shardName == ShardRegistry::kConfigServerShardId;
                })) {
                // There should be no namespaces that partially target config servers.
                invariant(endpoints.size() == 1);

                // For config servers, we do direct writes.
                Grid::get(opCtx)->catalogClient()->writeConfigServerDirect(
                    opCtx, request, response);
                return;
            }

            BatchWriteExec::executeBatch(opCtx, targeter, request, response, stats);
        }
    }
}

}  // namespace monger
