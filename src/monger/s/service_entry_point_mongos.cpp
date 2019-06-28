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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kNetwork

#include "monger/platform/basic.h"

#include "monger/s/service_entry_point_mongers.h"

#include "monger/db/auth/authorization_session.h"
#include "monger/db/commands.h"
#include "monger/db/curop.h"
#include "monger/db/dbmessage.h"
#include "monger/db/lasterror.h"
#include "monger/db/operation_context.h"
#include "monger/db/service_context.h"
#include "monger/rpc/message.h"
#include "monger/s/client/shard_connection.h"
#include "monger/s/cluster_last_error_info.h"
#include "monger/s/commands/strategy.h"
#include "monger/util/log.h"
#include "monger/util/scopeguard.h"

namespace monger {

namespace {

BSONObj buildErrReply(const DBException& ex) {
    BSONObjBuilder errB;
    errB.append("$err", ex.what());
    errB.append("code", ex.code());
    return errB.obj();
}

}  // namespace


DbResponse ServiceEntryPointMongers::handleRequest(OperationContext* opCtx, const Message& message) {
    // Release any cached egress connections for client back to pool before destroying
    auto guard = makeGuard(ShardConnection::releaseMyConnections);

    const int32_t msgId = message.header().getId();
    const NetworkOp op = message.operation();

    // This exception will not be returned to the caller, but will be logged and will close the
    // connection
    uassert(ErrorCodes::IllegalOperation,
            str::stream() << "Message type " << op << " is not supported.",
            isSupportedRequestNetworkOp(op) &&
                op != dbCompressed);  // Decompression should be handled above us.

    // Start a new LastError session. Any exceptions thrown from here onwards will be returned
    // to the caller (if the type of the message permits it).
    auto client = opCtx->getClient();
    if (!ClusterLastErrorInfo::get(client)) {
        ClusterLastErrorInfo::get(client) = std::make_shared<ClusterLastErrorInfo>();
    }
    ClusterLastErrorInfo::get(client)->newRequest();
    LastError::get(client).startRequest();
    AuthorizationSession::get(opCtx->getClient())->startRequest(opCtx);

    CurOp::get(opCtx)->ensureStarted();

    DbMessage dbm(message);

    // This is before the try block since it handles all exceptions that should not cause the
    // connection to close.
    if (op == dbMsg || (op == dbQuery && NamespaceString(dbm.getns()).isCommand())) {
        auto dbResponse = Strategy::clientCommand(opCtx, message);

        // Mark the op as complete, populate the response length, and log it if appropriate.
        CurOp::get(opCtx)->completeAndLogOperation(
            opCtx, logger::LogComponent::kCommand, dbResponse.response.size());

        return dbResponse;
    }

    NamespaceString nss;
    DbResponse dbResponse;
    try {
        if (dbm.messageShouldHaveNs()) {
            nss = NamespaceString(StringData(dbm.getns()));

            uassert(ErrorCodes::InvalidNamespace,
                    str::stream() << "Invalid ns [" << nss.ns() << "]",
                    nss.isValid());

            uassert(ErrorCodes::IllegalOperation,
                    "Can't use 'local' database through mongers",
                    nss.db() != NamespaceString::kLocalDb);
        }


        LOG(3) << "Request::process begin ns: " << nss << " msg id: " << msgId
               << " op: " << networkOpToString(op);

        switch (op) {
            case dbQuery:
                // Commands are handled above through Strategy::clientCommand().
                invariant(!nss.isCommand());
                opCtx->markKillOnClientDisconnect();
                dbResponse = Strategy::queryOp(opCtx, nss, &dbm);
                break;

            case dbGetMore:
                dbResponse = Strategy::getMore(opCtx, nss, &dbm);
                break;

            case dbKillCursors:
                Strategy::killCursors(opCtx, &dbm);  // No Response.
                break;

            case dbInsert:
            case dbUpdate:
            case dbDelete:
                Strategy::writeOp(opCtx, &dbm);  // No Response.
                break;

            default:
                MONGO_UNREACHABLE;
        }

        LOG(3) << "Request::process end ns: " << nss << " msg id: " << msgId
               << " op: " << networkOpToString(op);

    } catch (const DBException& ex) {
        LOG(1) << "Exception thrown while processing " << networkOpToString(op) << " op for "
               << nss.ns() << causedBy(ex);

        if (op == dbQuery || op == dbGetMore) {
            dbResponse = replyToQuery(buildErrReply(ex), ResultFlag_ErrSet);
        } else {
            // No Response.
        }

        // We *always* populate the last error for now
        LastError::get(opCtx->getClient()).setLastError(ex.code(), ex.what());
        CurOp::get(opCtx)->debug().errInfo = ex.toStatus();
    }

    // Mark the op as complete, populate the response length, and log it if appropriate.
    CurOp::get(opCtx)->completeAndLogOperation(
        opCtx, logger::LogComponent::kCommand, dbResponse.response.size());

    return dbResponse;
}

}  // namespace monger
