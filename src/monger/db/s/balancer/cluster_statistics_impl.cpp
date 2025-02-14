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

#include "monger/db/s/balancer/cluster_statistics_impl.h"

#include <algorithm>

#include "monger/base/status_with.h"
#include "monger/bson/util/bson_extract.h"
#include "monger/client/read_preference.h"
#include "monger/s/catalog/type_shard.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/grid.h"
#include "monger/s/shard_util.h"
#include "monger/util/log.h"
#include "monger/util/str.h"

namespace monger {
namespace {

const char kVersionField[] = "version";

/**
 * Executes the serverStatus command against the specified shard and obtains the version of the
 * running MongerD service.
 *
 * Returns the MongerD version in strig format or an error. Known error codes are:
 *  ShardNotFound if shard by that id is not available on the registry
 *  NoSuchKey if the version could not be retrieved
 */
StatusWith<std::string> retrieveShardMongerDVersion(OperationContext* opCtx, ShardId shardId) {
    auto shardRegistry = Grid::get(opCtx)->shardRegistry();
    auto shardStatus = shardRegistry->getShard(opCtx, shardId);
    if (!shardStatus.isOK()) {
        return shardStatus.getStatus();
    }
    auto shard = shardStatus.getValue();

    auto commandResponse =
        shard->runCommandWithFixedRetryAttempts(opCtx,
                                                ReadPreferenceSetting{ReadPreference::PrimaryOnly},
                                                "admin",
                                                BSON("serverStatus" << 1),
                                                Shard::RetryPolicy::kIdempotent);
    if (!commandResponse.isOK()) {
        return commandResponse.getStatus();
    }
    if (!commandResponse.getValue().commandStatus.isOK()) {
        return commandResponse.getValue().commandStatus;
    }

    BSONObj serverStatus = std::move(commandResponse.getValue().response);

    std::string version;
    Status status = bsonExtractStringField(serverStatus, kVersionField, &version);
    if (!status.isOK()) {
        return status;
    }

    return version;
}

}  // namespace

using ShardStatistics = ClusterStatistics::ShardStatistics;

ClusterStatisticsImpl::ClusterStatisticsImpl(BalancerRandomSource& random) : _random(random) {}

ClusterStatisticsImpl::~ClusterStatisticsImpl() = default;

StatusWith<std::vector<ShardStatistics>> ClusterStatisticsImpl::getStats(OperationContext* opCtx) {
    // Get a list of all the shards that are participating in this balance round along with any
    // maximum allowed quotas and current utilization. We get the latter by issuing
    // db.serverStatus() (mem.mapped) to all shards.
    //
    // TODO: skip unresponsive shards and mark information as stale.
    auto shardsStatus = Grid::get(opCtx)->catalogClient()->getAllShards(
        opCtx, repl::ReadConcernLevel::kMajorityReadConcern);
    if (!shardsStatus.isOK()) {
        return shardsStatus.getStatus();
    }

    auto& shards = shardsStatus.getValue().value;

    std::shuffle(shards.begin(), shards.end(), _random);

    std::vector<ShardStatistics> stats;

    for (const auto& shard : shards) {
        const auto shardSizeStatus = [&]() -> StatusWith<long long> {
            if (!shard.getMaxSizeMB()) {
                return 0;
            }

            return shardutil::retrieveTotalShardSize(opCtx, shard.getName());
        }();

        if (!shardSizeStatus.isOK()) {
            const auto& status = shardSizeStatus.getStatus();

            return status.withContext(str::stream()
                                      << "Unable to obtain shard utilization information for "
                                      << shard.getName());
        }

        std::string mongerDVersion;

        auto mongerDVersionStatus = retrieveShardMongerDVersion(opCtx, shard.getName());
        if (mongerDVersionStatus.isOK()) {
            mongerDVersion = std::move(mongerDVersionStatus.getValue());
        } else {
            // Since the mongerd version is only used for reporting, there is no need to fail the
            // entire round if it cannot be retrieved, so just leave it empty
            log() << "Unable to obtain shard version for " << shard.getName()
                  << causedBy(mongerDVersionStatus.getStatus());
        }

        std::set<std::string> shardTags;

        for (const auto& shardTag : shard.getTags()) {
            shardTags.insert(shardTag);
        }

        stats.emplace_back(shard.getName(),
                           shard.getMaxSizeMB(),
                           shardSizeStatus.getValue() / 1024 / 1024,
                           shard.getDraining(),
                           std::move(shardTags),
                           std::move(mongerDVersion));
    }

    return stats;
}

}  // namespace monger
