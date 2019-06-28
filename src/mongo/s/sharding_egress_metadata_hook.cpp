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

#include <string>

#include "monger/base/status.h"
#include "monger/db/service_context.h"
#include "monger/rpc/metadata/client_metadata_ismaster.h"
#include "monger/rpc/metadata/config_server_metadata.h"
#include "monger/rpc/metadata/impersonated_user_metadata.h"
#include "monger/rpc/metadata/metadata_hook.h"
#include "monger/rpc/metadata/repl_set_metadata.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/grid.h"
#include "monger/s/sharding_egress_metadata_hook.h"
#include "monger/util/net/hostandport.h"

namespace monger {
namespace rpc {

ShardingEgressMetadataHook::ShardingEgressMetadataHook(ServiceContext* serviceContext)
    : _serviceContext(serviceContext) {
    invariant(_serviceContext);
}

Status ShardingEgressMetadataHook::writeRequestMetadata(OperationContext* opCtx,
                                                        BSONObjBuilder* metadataBob) {
    try {
        writeAuthDataToImpersonatedUserMetadata(opCtx, metadataBob);
        ClientMetadataIsMasterState::writeToMetadata(opCtx, metadataBob);
        rpc::ConfigServerMetadata(_getConfigServerOpTime()).writeToMetadata(metadataBob);
        return Status::OK();
    } catch (...) {
        return exceptionToStatus();
    }
}

Status ShardingEgressMetadataHook::readReplyMetadata(OperationContext* opCtx,
                                                     StringData replySource,
                                                     const BSONObj& metadataObj) {
    try {
        _saveGLEStats(metadataObj, replySource);
        return _advanceConfigOpTimeFromShard(opCtx, replySource.toString(), metadataObj);
    } catch (...) {
        return exceptionToStatus();
    }
}

Status ShardingEgressMetadataHook::_advanceConfigOpTimeFromShard(OperationContext* opCtx,
                                                                 const ShardId& shardId,
                                                                 const BSONObj& metadataObj) {
    auto const grid = Grid::get(_serviceContext);

    try {
        auto shard = grid->shardRegistry()->getShardNoReload(shardId);
        if (!shard) {
            return Status::OK();
        }

        // Update our notion of the config server opTime from the configOpTime in the response.
        if (shard->isConfig()) {
            // Config servers return the config opTime as part of their own metadata.
            if (metadataObj.hasField(rpc::kReplSetMetadataFieldName)) {
                // Sharding users of ReplSetMetadata do not use the wall clock time field.
                auto parseStatus =
                    rpc::ReplSetMetadata::readFromMetadata(metadataObj, /*requireWallTime*/ false);
                if (!parseStatus.isOK()) {
                    return parseStatus.getStatus();
                }

                // Use the last committed optime to advance config optime.
                // For successful majority writes, we could use the optime of the last op
                // from us and lastOpCommitted is always greater than or equal to it.
                // On majority write failures, the last visible optime would be incorrect
                // due to rollback as explained in SERVER-24630 and the last committed optime
                // is safe to use.
                const auto& replMetadata = parseStatus.getValue();
                const auto opTime = replMetadata.getLastOpCommitted();
                grid->advanceConfigOpTime(opCtx, opTime.opTime, "reply from config server node");
            }
        } else {
            // Regular shards return the config opTime as part of ConfigServerMetadata.
            auto parseStatus = rpc::ConfigServerMetadata::readFromMetadata(metadataObj);
            if (!parseStatus.isOK()) {
                return parseStatus.getStatus();
            }

            const auto& configMetadata = parseStatus.getValue();
            const auto opTime = configMetadata.getOpTime();
            if (opTime.is_initialized()) {
                grid->advanceConfigOpTime(opCtx,
                                          opTime.get(),
                                          str::stream() << "reply from shard " << shardId
                                                        << " node");
            }
        }
        return Status::OK();
    } catch (...) {
        return exceptionToStatus();
    }
}

}  // namespace rpc
}  // namespace monger
