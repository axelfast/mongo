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

#include <vector>

#include "monger/bson/util/bson_extract.h"
#include "monger/db/commands.h"
#include "monger/db/query/count_command_as_aggregation_command.h"
#include "monger/db/query/count_command_gen.h"
#include "monger/db/query/view_response_formatter.h"
#include "monger/db/views/resolved_view.h"
#include "monger/rpc/get_status_from_command_result.h"
#include "monger/s/catalog_cache.h"
#include "monger/s/cluster_commands_helpers.h"
#include "monger/s/commands/cluster_explain.h"
#include "monger/s/commands/strategy.h"
#include "monger/s/grid.h"
#include "monger/s/query/cluster_aggregate.h"
#include "monger/util/timer.h"

namespace monger {
namespace {

class ClusterCountCmd : public ErrmsgCommandDeprecated {
public:
    ClusterCountCmd() : ErrmsgCommandDeprecated("count") {}

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }

    bool adminOnly() const override {
        return false;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }

    void addRequiredPrivileges(const std::string& dbname,
                               const BSONObj& cmdObj,
                               std::vector<Privilege>* out) const override {
        ActionSet actions;
        actions.addAction(ActionType::find);
        out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
    }

    bool errmsgRun(OperationContext* opCtx,
                   const std::string& dbname,
                   const BSONObj& cmdObj,
                   std::string& errmsg,
                   BSONObjBuilder& result) override {
        CommandHelpers::handleMarkKillOnClientDisconnect(opCtx);
        const NamespaceString nss(parseNs(dbname, cmdObj));
        uassert(ErrorCodes::InvalidNamespace,
                str::stream() << "Invalid namespace specified '" << nss.ns() << "'",
                nss.isValid());

        long long skip = 0;

        if (cmdObj["skip"].isNumber()) {
            skip = cmdObj["skip"].numberLong();
            if (skip < 0) {
                errmsg = "skip value is negative in count query";
                return false;
            }
        } else if (cmdObj["skip"].ok()) {
            errmsg = "skip value is not a valid number";
            return false;
        }

        BSONObjBuilder countCmdBuilder;
        countCmdBuilder.append("count", nss.coll());

        BSONObj filter;
        if (cmdObj["query"].isABSONObj()) {
            countCmdBuilder.append("query", cmdObj["query"].Obj());
            filter = cmdObj["query"].Obj();
        }

        BSONObj collation;
        BSONElement collationElement;
        auto status =
            bsonExtractTypedField(cmdObj, "collation", BSONType::Object, &collationElement);
        if (status.isOK()) {
            collation = collationElement.Obj();
        } else if (status != ErrorCodes::NoSuchKey) {
            uassertStatusOK(status);
        }

        if (cmdObj["limit"].isNumber()) {
            long long limit = cmdObj["limit"].numberLong();

            // We only need to factor in the skip value when sending to the shards if we
            // have a value for limit, otherwise, we apply it only once we have collected all
            // counts.
            if (limit != 0 && cmdObj["skip"].isNumber()) {
                if (limit > 0)
                    limit += skip;
                else
                    limit -= skip;
            }

            countCmdBuilder.append("limit", limit);
        }

        const std::initializer_list<StringData> passthroughFields = {
            "$queryOptions", "collation", "hint", "readConcern", QueryRequest::cmdOptionMaxTimeMS,
        };
        for (auto name : passthroughFields) {
            if (auto field = cmdObj[name]) {
                countCmdBuilder.append(field);
            }
        }

        auto countCmdObj = countCmdBuilder.done();

        std::vector<AsyncRequestsSender::Response> shardResponses;
        try {
            const auto routingInfo = uassertStatusOK(
                Grid::get(opCtx)->catalogCache()->getCollectionRoutingInfo(opCtx, nss));
            shardResponses =
                scatterGatherVersionedTargetByRoutingTable(opCtx,
                                                           nss.db(),
                                                           nss,
                                                           routingInfo,
                                                           countCmdObj,
                                                           ReadPreferenceSetting::get(opCtx),
                                                           Shard::RetryPolicy::kIdempotent,
                                                           filter,
                                                           collation);
        } catch (const ExceptionFor<ErrorCodes::CommandOnShardedViewNotSupportedOnMongerd>& ex) {
            // Rewrite the count command as an aggregation.

            auto countRequest = CountCommand::parse(IDLParserErrorContext("count"), cmdObj);
            auto aggCmdOnView =
                uassertStatusOK(countCommandAsAggregationCommand(countRequest, nss));
            auto aggRequestOnView =
                uassertStatusOK(AggregationRequest::parseFromBSON(nss, aggCmdOnView));

            auto resolvedAggRequest = ex->asExpandedViewAggregation(aggRequestOnView);
            auto resolvedAggCmd = resolvedAggRequest.serializeToCommandObj().toBson();

            BSONObj aggResult = CommandHelpers::runCommandDirectly(
                opCtx, OpMsgRequest::fromDBAndBody(dbname, std::move(resolvedAggCmd)));

            result.resetToEmpty();
            ViewResponseFormatter formatter(aggResult);
            auto formatStatus = formatter.appendAsCountResponse(&result);
            uassertStatusOK(formatStatus);

            return true;
        } catch (const ExceptionFor<ErrorCodes::NamespaceNotFound>&) {
            // If there's no collection with this name, the count aggregation behavior below
            // will produce a total count of 0.
            shardResponses = {};
        }

        long long total = 0;
        BSONObjBuilder shardSubTotal(result.subobjStart("shards"));

        for (const auto& response : shardResponses) {
            auto status = response.swResponse.getStatus();
            if (status.isOK()) {
                status = getStatusFromCommandResult(response.swResponse.getValue().data);
                if (status.isOK()) {
                    long long shardCount = response.swResponse.getValue().data["n"].numberLong();
                    shardSubTotal.appendNumber(response.shardId.toString(), shardCount);
                    total += shardCount;
                    continue;
                }
            }

            shardSubTotal.doneFast();
            // Add error context so that you can see on which shard failed as well as details
            // about that error.
            uassertStatusOK(status.withContext(str::stream() << "failed on: " << response.shardId));
        }

        shardSubTotal.doneFast();
        total = applySkipLimit(total, cmdObj);
        result.appendNumber("n", total);
        return true;
    }

    Status explain(OperationContext* opCtx,
                   const OpMsgRequest& request,
                   ExplainOptions::Verbosity verbosity,
                   rpc::ReplyBuilderInterface* result) const override {
        std::string dbname = request.getDatabase().toString();
        const BSONObj& cmdObj = request.body;
        const NamespaceString nss(parseNs(dbname, cmdObj));
        uassert(ErrorCodes::InvalidNamespace,
                str::stream() << "Invalid namespace specified '" << nss.ns() << "'",
                nss.isValid());

        // Extract the targeting query.
        BSONObj targetingQuery;
        if (Object == cmdObj["query"].type()) {
            targetingQuery = cmdObj["query"].Obj();
        }

        // Extract the targeting collation.
        BSONObj targetingCollation;
        BSONElement targetingCollationElement;
        auto status = bsonExtractTypedField(
            cmdObj, "collation", BSONType::Object, &targetingCollationElement);
        if (status.isOK()) {
            targetingCollation = targetingCollationElement.Obj();
        } else if (status != ErrorCodes::NoSuchKey) {
            return status;
        }

        const auto explainCmd = ClusterExplain::wrapAsExplain(cmdObj, verbosity);

        // We will time how long it takes to run the commands on the shards
        Timer timer;

        std::vector<AsyncRequestsSender::Response> shardResponses;
        try {
            const auto routingInfo = uassertStatusOK(
                Grid::get(opCtx)->catalogCache()->getCollectionRoutingInfo(opCtx, nss));
            shardResponses =
                scatterGatherVersionedTargetByRoutingTable(opCtx,
                                                           nss.db(),
                                                           nss,
                                                           routingInfo,
                                                           explainCmd,
                                                           ReadPreferenceSetting::get(opCtx),
                                                           Shard::RetryPolicy::kIdempotent,
                                                           targetingQuery,
                                                           targetingCollation);
        } catch (const ExceptionFor<ErrorCodes::CommandOnShardedViewNotSupportedOnMongerd>& ex) {
            CountCommand countRequest(NamespaceStringOrUUID(NamespaceString{}));
            try {
                countRequest = CountCommand::parse(IDLParserErrorContext("count"), cmdObj);
            } catch (...) {
                return exceptionToStatus();
            }

            auto aggCmdOnView = countCommandAsAggregationCommand(countRequest, nss);
            if (!aggCmdOnView.isOK()) {
                return aggCmdOnView.getStatus();
            }

            auto aggRequestOnView =
                AggregationRequest::parseFromBSON(nss, aggCmdOnView.getValue(), verbosity);
            if (!aggRequestOnView.isOK()) {
                return aggRequestOnView.getStatus();
            }

            auto bodyBuilder = result->getBodyBuilder();
            // An empty PrivilegeVector is acceptable because these privileges are only checked on
            // getMore and explain will not open a cursor.
            return ClusterAggregate::retryOnViewError(opCtx,
                                                      aggRequestOnView.getValue(),
                                                      *ex.extraInfo<ResolvedView>(),
                                                      nss,
                                                      PrivilegeVector(),
                                                      &bodyBuilder);
        }

        long long millisElapsed = timer.millis();

        const char* mongersStageName =
            ClusterExplain::getStageNameForReadOp(shardResponses.size(), cmdObj);

        auto bodyBuilder = result->getBodyBuilder();
        return ClusterExplain::buildExplainResult(
            opCtx,
            ClusterExplain::downconvert(opCtx, shardResponses),
            mongersStageName,
            millisElapsed,
            &bodyBuilder);
    }

private:
    static long long applySkipLimit(long long num, const BSONObj& cmd) {
        BSONElement s = cmd["skip"];
        BSONElement l = cmd["limit"];

        if (s.isNumber()) {
            num = num - s.numberLong();
            if (num < 0) {
                num = 0;
            }
        }

        if (l.isNumber()) {
            long long limit = l.numberLong();
            if (limit < 0) {
                limit = -limit;
            }

            // 0 limit means no limit
            if (limit < num && limit != 0) {
                num = limit;
            }
        }

        return num;
    }

} clusterCountCmd;

}  // namespace
}  // namespace monger
