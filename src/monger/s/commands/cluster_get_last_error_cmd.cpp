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

#include <vector>

#include "monger/client/remote_command_targeter.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/lasterror.h"
#include "monger/executor/task_executor_pool.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/cluster_last_error_info.h"
#include "monger/s/grid.h"
#include "monger/s/multi_statement_transaction_requests_sender.h"
#include "monger/s/write_ops/batch_downconvert.h"
#include "monger/util/log.h"

namespace monger {
namespace {

using std::vector;

// Adds a wOpTime and a wElectionId field to a set of gle options
BSONObj buildGLECmdWithOpTime(const BSONObj& gleOptions,
                              const repl::OpTime& opTime,
                              const OID& electionId) {
    BSONObjBuilder builder;
    BSONObjIterator it(gleOptions);

    for (int i = 0; it.more(); ++i) {
        BSONElement el = it.next();

        // Make sure first element is getLastError : 1
        if (i == 0) {
            StringData elName(el.fieldName());
            if (!elName.equalCaseInsensitive("getLastError")) {
                builder.append("getLastError", 1);
            }
        }

        builder.append(el);
    }
    opTime.append(&builder, "wOpTime");
    builder.appendOID("wElectionId", const_cast<OID*>(&electionId));
    return builder.obj();
}

/**
 * Uses GLE and the shard hosts and opTimes last written by write commands to enforce a
 * write concern across the previously used shards.
 *
 * Returns OK with the LegacyWCResponses containing only write concern error information
 * Returns !OK if there was an error getting a GLE response
 */
Status enforceLegacyWriteConcern(OperationContext* opCtx,
                                 StringData dbName,
                                 const BSONObj& options,
                                 const HostOpTimeMap& hostOpTimes,
                                 std::vector<LegacyWCResponse>* legacyWCResponses) {
    if (hostOpTimes.empty()) {
        return Status::OK();
    }

    // Assemble requests
    std::vector<AsyncRequestsSender::Request> requests;
    for (HostOpTimeMap::const_iterator it = hostOpTimes.begin(); it != hostOpTimes.end(); ++it) {
        const ConnectionString& shardConnStr = it->first;
        const auto& hot = it->second;
        const repl::OpTime& opTime = hot.opTime;
        const OID& electionId = hot.electionId;

        auto swShard = Grid::get(opCtx)->shardRegistry()->getShard(opCtx, shardConnStr.toString());
        if (!swShard.isOK()) {
            return swShard.getStatus();
        }

        LOG(3) << "enforcing write concern " << options << " on " << shardConnStr.toString()
               << " at opTime " << opTime.getTimestamp().toStringPretty() << " with electionID "
               << electionId;

        BSONObj gleCmd = buildGLECmdWithOpTime(options, opTime, electionId);
        requests.emplace_back(swShard.getValue()->getId(), gleCmd);
    }

    // Send the requests.

    const ReadPreferenceSetting readPref(ReadPreference::PrimaryOnly, TagSet());
    MultiStatementTransactionRequestsSender ars(
        opCtx,
        Grid::get(opCtx)->getExecutorPool()->getArbitraryExecutor(),
        dbName.toString(),
        requests,
        readPref,
        Shard::RetryPolicy::kIdempotent);

    // Receive the responses.

    vector<Status> failedStatuses;
    while (!ars.done()) {
        // Block until a response is available.
        auto response = ars.next();

        // Return immediately if we failed to contact a shard.
        if (!response.shardHostAndPort) {
            invariant(!response.swResponse.isOK());
            return response.swResponse.getStatus();
        }

        // We successfully contacted the shard, but it returned some error.
        if (!response.swResponse.isOK()) {
            failedStatuses.push_back(std::move(response.swResponse.getStatus()));
            continue;
        }

        BSONObj gleResponse = stripNonWCInfo(response.swResponse.getValue().data);

        // Use the downconversion tools to determine if this GLE response is ok, a
        // write concern error, or an unknown error we should immediately abort for.
        GLEErrors errors;
        Status extractStatus = extractGLEErrors(gleResponse, &errors);
        if (!extractStatus.isOK()) {
            failedStatuses.push_back(extractStatus);
            continue;
        }

        LegacyWCResponse wcResponse;
        invariant(response.shardHostAndPort);
        wcResponse.shardHost = response.shardHostAndPort->toString();
        wcResponse.gleResponse = gleResponse;
        if (errors.wcError.get()) {
            wcResponse.errToReport = errors.wcError->toString();
        }

        legacyWCResponses->push_back(wcResponse);
    }

    if (failedStatuses.empty()) {
        return Status::OK();
    }

    StringBuilder builder;
    builder << "could not enforce write concern";

    for (vector<Status>::const_iterator it = failedStatuses.begin(); it != failedStatuses.end();
         ++it) {
        const Status& failedStatus = *it;
        if (it == failedStatuses.begin()) {
            builder << causedBy(failedStatus.toString());
        } else {
            builder << ":: and ::" << failedStatus.toString();
        }
    }

    if (failedStatuses.size() == 1u) {
        return failedStatuses.front();
    } else {
        return Status(ErrorCodes::MultipleErrorsOccurred, builder.str());
    }
}


class GetLastErrorCmd : public BasicCommand {
public:
    GetLastErrorCmd() : BasicCommand("getLastError", "getlasterror") {}

    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }

    std::string help() const override {
        return "check for an error on the last command executed";
    }

    virtual void addRequiredPrivileges(const std::string& dbname,
                                       const BSONObj& cmdObj,
                                       std::vector<Privilege>* out) const {
        // No auth required for getlasterror
    }

    bool requiresAuth() const override {
        return false;
    }

    virtual bool run(OperationContext* opCtx,
                     const std::string& dbname,
                     const BSONObj& cmdObj,
                     BSONObjBuilder& result) {
        // Mongers GLE - finicky.
        //
        // To emulate mongerd, we first append any write errors we had, then try to append
        // write concern error if there was no write error.  We need to contact the previous
        // shards regardless to maintain 2.4 behavior.
        //
        // If there are any unexpected or connectivity errors when calling GLE, fail the
        // command.
        //
        // Finally, report the write concern errors IF we don't already have an error.
        // If we only get one write concern error back, report that, otherwise report an
        // aggregated error.
        //
        // TODO: Do we need to contact the prev shards regardless - do we care that much
        // about 2.4 behavior?
        //

        LastError* le = &LastError::get(cc());
        le->disable();


        // Write commands always have the error stored in the mongers last error
        bool errorOccurred = false;
        if (le->getNPrev() == 1) {
            errorOccurred = le->appendSelf(result, false);
        }

        // For compatibility with 2.4 sharded GLE, we always enforce the write concern
        // across all shards.
        const HostOpTimeMap hostOpTimes(ClusterLastErrorInfo::get(cc())->getPrevHostOpTimes());

        std::vector<LegacyWCResponse> wcResponses;
        auto status =
            enforceLegacyWriteConcern(opCtx,
                                      dbname,
                                      CommandHelpers::filterCommandRequestForPassthrough(cmdObj),
                                      hostOpTimes,
                                      &wcResponses);

        // Don't forget about our last hosts, reset the client info
        ClusterLastErrorInfo::get(cc())->disableForCommand();

        // We're now done contacting all remote servers, just report results

        if (!status.isOK()) {
            // Return immediately if we failed to contact a shard, unexpected GLE issue
            // Can't return code, since it may have been set above (2.4 compatibility)
            result.append("errmsg", status.reason());
            return false;
        }

        // Go through all the write concern responses and find errors
        BSONArrayBuilder shards;
        BSONObjBuilder shardRawGLE;
        BSONArrayBuilder errors;
        BSONArrayBuilder errorRawGLE;

        int numWCErrors = 0;
        const LegacyWCResponse* lastErrResponse = nullptr;

        for (std::vector<LegacyWCResponse>::const_iterator it = wcResponses.begin();
             it != wcResponses.end();
             ++it) {
            const LegacyWCResponse& wcResponse = *it;

            shards.append(wcResponse.shardHost);
            shardRawGLE.append(wcResponse.shardHost, wcResponse.gleResponse);

            if (!wcResponse.errToReport.empty()) {
                numWCErrors++;
                lastErrResponse = &wcResponse;
                errors.append(wcResponse.errToReport);
                errorRawGLE.append(wcResponse.gleResponse);
            }
        }

        // Always report what we found to match 2.4 behavior and for debugging
        if (wcResponses.size() == 1u) {
            result.append("singleShard", wcResponses.front().shardHost);
        } else {
            result.append("shards", shards.arr());
            result.append("shardRawGLE", shardRawGLE.obj());
        }

        // Suppress write concern errors if a write error occurred, to match mongerd behavior
        if (errorOccurred || numWCErrors == 0) {
            // Still need to return err
            if (!errorOccurred) {
                result.appendNull("err");
            }

            return true;
        }

        if (numWCErrors == 1) {
            // Return the single write concern error we found, err should be set or not
            // from gle response
            CommandHelpers::filterCommandReplyForPassthrough(lastErrResponse->gleResponse, &result);
            return lastErrResponse->gleResponse["ok"].trueValue();
        } else {
            // Return a generic combined WC error message
            result.append("errs", errors.arr());
            result.append("errObjects", errorRawGLE.arr());

            // Need to always return err
            result.appendNull("err");

            return CommandHelpers::appendCommandStatusNoThrow(
                result,
                Status(ErrorCodes::WriteConcernFailed, "multiple write concern errors occurred"));
        }
    }

} cmdGetLastError;

}  // namespace
}  // namespace monger
