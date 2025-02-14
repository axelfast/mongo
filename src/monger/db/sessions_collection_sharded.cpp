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

#include "monger/db/sessions_collection_sharded.h"

#include "monger/db/matcher/extensions_callback_noop.h"
#include "monger/db/operation_context.h"
#include "monger/db/query/canonical_query.h"
#include "monger/db/query/query_request.h"
#include "monger/db/sessions_collection_rs.h"
#include "monger/rpc/get_status_from_command_result.h"
#include "monger/rpc/op_msg.h"
#include "monger/rpc/op_msg_rpc_impls.h"
#include "monger/s/catalog_cache.h"
#include "monger/s/client/shard.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/grid.h"
#include "monger/s/query/cluster_find.h"
#include "monger/s/write_ops/batch_write_exec.h"
#include "monger/s/write_ops/batched_command_request.h"
#include "monger/s/write_ops/batched_command_response.h"
#include "monger/s/write_ops/cluster_write.h"

namespace monger {

namespace {

BSONObj lsidQuery(const LogicalSessionId& lsid) {
    return BSON(LogicalSessionRecord::kIdFieldName << lsid.toBSON());
}

}  // namespace

Status SessionsCollectionSharded::_checkCacheForSessionsCollection(OperationContext* opCtx) {
    // If the sharding state is not yet initialized, fail.
    if (!Grid::get(opCtx)->isShardingInitialized()) {
        return {ErrorCodes::ShardingStateNotInitialized, "sharding state is not yet initialized"};
    }

    // If the collection doesn't exist, fail. Only the config servers generate it.
    auto res = Grid::get(opCtx)->catalogCache()->getShardedCollectionRoutingInfoWithRefresh(
        opCtx, NamespaceString::kLogicalSessionsNamespace);
    if (!res.isOK()) {
        return res.getStatus();
    }

    auto routingInfo = res.getValue();
    if (routingInfo.cm()) {
        return Status::OK();
    }

    return {ErrorCodes::NamespaceNotFound, "config.system.sessions does not exist"};
}

std::vector<LogicalSessionId> SessionsCollectionSharded::_groupSessionIdsByOwningShard(
    OperationContext* opCtx, const LogicalSessionIdSet& sessions) {
    auto routingInfo = uassertStatusOK(Grid::get(opCtx)->catalogCache()->getCollectionRoutingInfo(
        opCtx, NamespaceString::kLogicalSessionsNamespace));
    auto cm = routingInfo.cm();

    uassert(ErrorCodes::NamespaceNotSharded,
            str::stream() << "Collection " << NamespaceString::kLogicalSessionsNamespace
                          << " is not sharded",
            cm);

    std::multimap<ShardId, LogicalSessionId> sessionIdsByOwningShard;
    for (const auto& session : sessions) {
        sessionIdsByOwningShard.emplace(
            cm->findIntersectingChunkWithSimpleCollation(session.getId().toBSON()).getShardId(),
            session);
    }

    std::vector<LogicalSessionId> sessionIdsGroupedByShard;
    sessionIdsGroupedByShard.reserve(sessions.size());
    for (auto& session : sessionIdsByOwningShard) {
        sessionIdsGroupedByShard.emplace_back(std::move(session.second));
    }

    return sessionIdsGroupedByShard;
}

std::vector<LogicalSessionRecord> SessionsCollectionSharded::_groupSessionRecordsByOwningShard(
    OperationContext* opCtx, const LogicalSessionRecordSet& sessions) {
    auto routingInfo = uassertStatusOK(Grid::get(opCtx)->catalogCache()->getCollectionRoutingInfo(
        opCtx, NamespaceString::kLogicalSessionsNamespace));
    auto cm = routingInfo.cm();

    uassert(ErrorCodes::NamespaceNotSharded,
            str::stream() << "Collection " << NamespaceString::kLogicalSessionsNamespace
                          << " is not sharded",
            cm);

    std::multimap<ShardId, LogicalSessionRecord> sessionsByOwningShard;
    for (const auto& session : sessions) {
        sessionsByOwningShard.emplace(
            cm->findIntersectingChunkWithSimpleCollation(session.getId().toBSON()).getShardId(),
            session);
    }

    std::vector<LogicalSessionRecord> sessionRecordsGroupedByShard;
    sessionRecordsGroupedByShard.reserve(sessions.size());
    for (auto& session : sessionsByOwningShard) {
        sessionRecordsGroupedByShard.emplace_back(std::move(session.second));
    }

    return sessionRecordsGroupedByShard;
}

Status SessionsCollectionSharded::setupSessionsCollection(OperationContext* opCtx) {
    return checkSessionsCollectionExists(opCtx);
}

Status SessionsCollectionSharded::checkSessionsCollectionExists(OperationContext* opCtx) {
    return _checkCacheForSessionsCollection(opCtx);
}

Status SessionsCollectionSharded::refreshSessions(OperationContext* opCtx,
                                                  const LogicalSessionRecordSet& sessions) {
    auto send = [&](BSONObj toSend) {
        auto opMsg =
            OpMsgRequest::fromDBAndBody(NamespaceString::kLogicalSessionsNamespace.db(), toSend);
        auto request = BatchedCommandRequest::parseUpdate(opMsg);

        BatchedCommandResponse response;
        BatchWriteExecStats stats;

        ClusterWriter::write(opCtx, request, &stats, &response);
        return response.toStatus();
    };

    return doRefresh(NamespaceString::kLogicalSessionsNamespace,
                     _groupSessionRecordsByOwningShard(opCtx, sessions),
                     send);
}

Status SessionsCollectionSharded::removeRecords(OperationContext* opCtx,
                                                const LogicalSessionIdSet& sessions) {
    auto send = [&](BSONObj toSend) {
        auto opMsg =
            OpMsgRequest::fromDBAndBody(NamespaceString::kLogicalSessionsNamespace.db(), toSend);
        auto request = BatchedCommandRequest::parseDelete(opMsg);

        BatchedCommandResponse response;
        BatchWriteExecStats stats;

        ClusterWriter::write(opCtx, request, &stats, &response);
        return response.toStatus();
    };

    return doRemove(NamespaceString::kLogicalSessionsNamespace,
                    _groupSessionIdsByOwningShard(opCtx, sessions),
                    send);
}

StatusWith<LogicalSessionIdSet> SessionsCollectionSharded::findRemovedSessions(
    OperationContext* opCtx, const LogicalSessionIdSet& sessions) {

    auto send = [&](BSONObj toSend) -> StatusWith<BSONObj> {
        auto qr = QueryRequest::makeFromFindCommand(
            NamespaceString::kLogicalSessionsNamespace, toSend, false);
        if (!qr.isOK()) {
            return qr.getStatus();
        }

        const boost::intrusive_ptr<ExpressionContext> expCtx;
        auto cq = CanonicalQuery::canonicalize(opCtx,
                                               std::move(qr.getValue()),
                                               expCtx,
                                               ExtensionsCallbackNoop(),
                                               MatchExpressionParser::kBanAllSpecialFeatures);
        if (!cq.isOK()) {
            return cq.getStatus();
        }

        // Do the work to generate the first batch of results. This blocks waiting to get responses
        // from the shard(s).
        std::vector<BSONObj> batch;
        CursorId cursorId;
        try {
            cursorId = ClusterFind::runQuery(
                opCtx, *cq.getValue(), ReadPreferenceSetting::get(opCtx), &batch);
        } catch (const DBException& ex) {
            return ex.toStatus();
        }

        rpc::OpMsgReplyBuilder replyBuilder;
        CursorResponseBuilder::Options options;
        options.isInitialResponse = true;
        CursorResponseBuilder firstBatch(&replyBuilder, options);
        for (const auto& obj : batch) {
            firstBatch.append(obj);
        }
        firstBatch.done(cursorId, NamespaceString::kLogicalSessionsNamespace.ns());

        return replyBuilder.releaseBody();
    };

    return doFindRemoved(NamespaceString::kLogicalSessionsNamespace,
                         _groupSessionIdsByOwningShard(opCtx, sessions),
                         send);
}

}  // namespace monger
