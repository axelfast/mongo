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

#include "monger/db/s/sharding_state_recovery.h"

#include "monger/bson/bsonobj.h"
#include "monger/bson/bsonobjbuilder.h"
#include "monger/bson/util/bson_extract.h"
#include "monger/client/connection_string.h"
#include "monger/db/catalog_raii.h"
#include "monger/db/dbhelpers.h"
#include "monger/db/namespace_string.h"
#include "monger/db/operation_context.h"
#include "monger/db/ops/update.h"
#include "monger/db/ops/update_request.h"
#include "monger/db/repl/bson_extract_optime.h"
#include "monger/db/repl/optime.h"
#include "monger/db/repl/repl_client_info.h"
#include "monger/db/s/sharding_logging.h"
#include "monger/db/s/sharding_state.h"
#include "monger/db/write_concern.h"
#include "monger/db/write_concern_options.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/grid.h"
#include "monger/util/log.h"

namespace monger {
namespace {

const char kRecoveryDocumentId[] = "minOpTimeRecovery";
const char kMinOpTime[] = "minOpTime";
const char kMinOpTimeUpdaters[] = "minOpTimeUpdaters";

const WriteConcernOptions kMajorityWriteConcern(WriteConcernOptions::kMajority,
                                                WriteConcernOptions::SyncMode::UNSET,
                                                WriteConcernOptions::kWriteConcernTimeoutSharding);

const WriteConcernOptions kLocalWriteConcern(1,
                                             WriteConcernOptions::SyncMode::UNSET,
                                             Milliseconds(0));

/**
 * Encapsulates the parsing and construction of the config server min opTime recovery document.
 */
class RecoveryDocument {
public:
    enum ChangeType : int8_t { Increment = 1, Decrement = -1, Clear = 0 };

    static StatusWith<RecoveryDocument> fromBSON(const BSONObj& obj) {
        RecoveryDocument recDoc;

        Status status = bsonExtractOpTimeField(obj, kMinOpTime, &recDoc._minOpTime);
        if (!status.isOK())
            return status;

        status = bsonExtractIntegerField(obj, kMinOpTimeUpdaters, &recDoc._minOpTimeUpdaters);
        if (!status.isOK())
            return status;

        return recDoc;
    }

    static BSONObj createChangeObj(repl::OpTime minOpTime, ChangeType change) {
        BSONObjBuilder cmdBuilder;

        {
            BSONObjBuilder setBuilder(cmdBuilder.subobjStart("$set"));
            minOpTime.append(&setBuilder, kMinOpTime);
        }

        if (change == Clear) {
            cmdBuilder.append("$set", BSON(kMinOpTimeUpdaters << 0));
        } else {
            cmdBuilder.append("$inc", BSON(kMinOpTimeUpdaters << change));
        }

        return cmdBuilder.obj();
    }

    static BSONObj getQuery() {
        return BSON("_id" << kRecoveryDocumentId);
    }

    BSONObj toBSON() const {
        BSONObjBuilder builder;
        builder.append("_id", kRecoveryDocumentId);
        builder.append(kMinOpTime, _minOpTime.toBSON());
        builder.append(kMinOpTimeUpdaters, _minOpTimeUpdaters);

        return builder.obj();
    }

    repl::OpTime getMinOpTime() const {
        return _minOpTime;
    }

    int64_t getMinOpTimeUpdaters() const {
        return _minOpTimeUpdaters;
    }

private:
    RecoveryDocument() = default;
    repl::OpTime _minOpTime;
    long long _minOpTimeUpdaters;
};

/**
 * This method is the main entry point for updating the sharding state recovery document. The goal
 * it has is to always move the opTime forward for a currently running server. It achieves this by
 * serializing the modify calls and reading the current opTime under X-lock on the admin database.
 */
Status modifyRecoveryDocument(OperationContext* opCtx,
                              RecoveryDocument::ChangeType change,
                              const WriteConcernOptions& writeConcern) {
    try {
        // Use boost::optional so we can release the locks early
        boost::optional<AutoGetOrCreateDb> autoGetOrCreateDb;
        autoGetOrCreateDb.emplace(
            opCtx, NamespaceString::kServerConfigurationNamespace.db(), MODE_X);

        // The config server connection string and shard name are no longer parsed in 4.0, but 3.6
        // nodes still expect to find them, so we must include them until after 4.0 ships.
        auto const grid = Grid::get(opCtx);
        BSONObj updateObj = RecoveryDocument::createChangeObj(grid->configOpTime(), change);

        LOG(1) << "Changing sharding recovery document " << redact(updateObj);

        UpdateRequest updateReq(NamespaceString::kServerConfigurationNamespace);
        updateReq.setQuery(RecoveryDocument::getQuery());
        updateReq.setUpdateModification(updateObj);
        updateReq.setUpsert();

        UpdateResult result = update(opCtx, autoGetOrCreateDb->getDb(), updateReq);
        invariant(result.numDocsModified == 1 || !result.upserted.isEmpty());
        invariant(result.numMatched <= 1);

        // Wait until the majority write concern has been satisfied, but do it outside of lock
        autoGetOrCreateDb = boost::none;

        WriteConcernResult writeConcernResult;
        return waitForWriteConcern(opCtx,
                                   repl::ReplClientInfo::forClient(opCtx->getClient()).getLastOp(),
                                   writeConcern,
                                   &writeConcernResult);
    } catch (const DBException& ex) {
        return ex.toStatus();
    }
}

}  // namespace

Status ShardingStateRecovery::startMetadataOp(OperationContext* opCtx) {
    Status upsertStatus =
        modifyRecoveryDocument(opCtx, RecoveryDocument::Increment, kMajorityWriteConcern);

    if (upsertStatus == ErrorCodes::WriteConcernFailed) {
        // Couldn't wait for the replication to complete, but the local write was performed. Clear
        // it up fast (without any waiting for journal or replication) and still treat it as
        // failure.
        modifyRecoveryDocument(opCtx, RecoveryDocument::Decrement, WriteConcernOptions())
            .transitional_ignore();
    }

    return upsertStatus;
}

void ShardingStateRecovery::endMetadataOp(OperationContext* opCtx) {
    Status status =
        modifyRecoveryDocument(opCtx, RecoveryDocument::Decrement, WriteConcernOptions());
    if (!status.isOK()) {
        warning() << "Failed to decrement minOpTimeUpdaters due to " << redact(status);
    }
}

Status ShardingStateRecovery::recover(OperationContext* opCtx) {
    Grid* const grid = Grid::get(opCtx);
    ShardingState* const shardingState = ShardingState::get(opCtx);
    invariant(shardingState->enabled());

    BSONObj recoveryDocBSON;

    try {
        AutoGetCollection autoColl(opCtx, NamespaceString::kServerConfigurationNamespace, MODE_IS);
        if (!Helpers::findOne(
                opCtx, autoColl.getCollection(), RecoveryDocument::getQuery(), recoveryDocBSON)) {
            return Status::OK();
        }
    } catch (const DBException& ex) {
        return ex.toStatus();
    }

    const auto recoveryDocStatus = RecoveryDocument::fromBSON(recoveryDocBSON);
    if (!recoveryDocStatus.isOK())
        return recoveryDocStatus.getStatus();

    const auto recoveryDoc = std::move(recoveryDocStatus.getValue());

    log() << "Sharding state recovery process found document " << redact(recoveryDoc.toBSON());

    if (!recoveryDoc.getMinOpTimeUpdaters()) {
        // Treat the minOpTime as up-to-date
        const auto prevOpTime = grid->advanceConfigOpTime(
            opCtx, recoveryDoc.getMinOpTime(), "sharding state recovery document");
        if (prevOpTime) {
            log()
                << "No in flight metadata change operations, so config server optime updated from "
                << *prevOpTime << " to " << recoveryDoc.getMinOpTime();
        }
        return Status::OK();
    }

    log() << "Sharding state recovery document indicates there were "
          << recoveryDoc.getMinOpTimeUpdaters()
          << " metadata change operations in flight. Contacting the config server primary in order "
             "to retrieve the most recent opTime.";

    // Need to fetch the latest uptime from the config server, so do a logging write
    Status status = ShardingLogging::get(opCtx)->logChangeChecked(
        opCtx,
        "Sharding minOpTime recovery",
        NamespaceString::kServerConfigurationNamespace.ns(),
        recoveryDocBSON,
        ShardingCatalogClient::kMajorityWriteConcern);
    if (!status.isOK())
        return status;

    log() << "Sharding state recovered. New config server opTime is " << grid->configOpTime();

    // Finally, clear the recovery document so next time we don't need to recover
    status = modifyRecoveryDocument(opCtx, RecoveryDocument::Clear, kLocalWriteConcern);
    if (!status.isOK()) {
        warning() << "Failed to reset sharding state recovery document due to " << redact(status);
    }

    return Status::OK();
}


}  // namespace monger
