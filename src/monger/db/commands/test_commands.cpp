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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kCommand

#include <string>

#include "monger/platform/basic.h"

#include "monger/base/init.h"
#include "monger/base/initializer_context.h"
#include "monger/db/catalog/capped_utils.h"
#include "monger/db/catalog/collection.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/commands/test_commands_enabled.h"
#include "monger/db/db_raii.h"
#include "monger/db/index_builds_coordinator.h"
#include "monger/db/op_observer.h"
#include "monger/db/query/internal_plans.h"
#include "monger/db/service_context.h"
#include "monger/util/log.h"

namespace monger {

using repl::UnreplicatedWritesBlock;
using std::endl;
using std::string;
using std::stringstream;

/* For testing only, not for general use. Enabled via command-line */
class GodInsert : public ErrmsgCommandDeprecated {
public:
    GodInsert() : ErrmsgCommandDeprecated("godinsert") {}
    virtual bool adminOnly() const {
        return false;
    }
    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }
    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }
    // No auth needed because it only works when enabled via command line.
    virtual void addRequiredPrivileges(const std::string& dbname,
                                       const BSONObj& cmdObj,
                                       std::vector<Privilege>* out) const {}
    std::string help() const override {
        return "internal. for testing only.";
    }
    virtual bool errmsgRun(OperationContext* opCtx,
                           const string& dbname,
                           const BSONObj& cmdObj,
                           string& errmsg,
                           BSONObjBuilder& result) {
        const NamespaceString nss(CommandHelpers::parseNsCollectionRequired(dbname, cmdObj));
        log() << "test only command godinsert invoked coll:" << nss.coll();
        BSONObj obj = cmdObj["obj"].embeddedObjectUserCheck();

        Lock::DBLock lk(opCtx, dbname, MODE_X);
        OldClientContext ctx(opCtx, nss.ns());
        Database* db = ctx.db();

        WriteUnitOfWork wunit(opCtx);
        UnreplicatedWritesBlock unreplicatedWritesBlock(opCtx);
        Collection* collection = db->getCollection(opCtx, nss);
        if (!collection) {
            collection = db->createCollection(opCtx, nss);
            if (!collection) {
                errmsg = "could not create collection";
                return false;
            }
        }
        OpDebug* const nullOpDebug = nullptr;
        Status status = collection->insertDocument(opCtx, InsertStatement(obj), nullOpDebug, false);
        if (status.isOK()) {
            wunit.commit();
        }
        uassertStatusOK(status);
        return true;
    }
};

MONGO_REGISTER_TEST_COMMAND(GodInsert);

// Testing only, enabled via command-line.
class CapTrunc : public BasicCommand {
public:
    CapTrunc() : BasicCommand("captrunc") {}
    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }
    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }
    // No auth needed because it only works when enabled via command line.
    virtual void addRequiredPrivileges(const std::string& dbname,
                                       const BSONObj& cmdObj,
                                       std::vector<Privilege>* out) const {}
    virtual bool run(OperationContext* opCtx,
                     const string& dbname,
                     const BSONObj& cmdObj,
                     BSONObjBuilder& result) {
        const NamespaceString fullNs = CommandHelpers::parseNsCollectionRequired(dbname, cmdObj);
        if (!fullNs.isValid()) {
            uasserted(ErrorCodes::InvalidNamespace,
                      str::stream() << "collection name " << fullNs.ns() << " is not valid");
        }

        int n = cmdObj.getIntField("n");
        bool inc = cmdObj.getBoolField("inc");  // inclusive range?

        if (n <= 0) {
            uasserted(ErrorCodes::BadValue, "n must be a positive integer");
        }

        // Lock the database in mode IX and lock the collection exclusively.
        AutoGetCollection autoColl(opCtx, fullNs, MODE_X);
        Collection* collection = autoColl.getCollection();
        if (!collection) {
            uasserted(ErrorCodes::NamespaceNotFound,
                      str::stream() << "collection " << fullNs.ns() << " does not exist");
        }

        if (!collection->isCapped()) {
            uasserted(ErrorCodes::IllegalOperation, "collection must be capped");
        }

        RecordId end;
        {
            // Scan backwards through the collection to find the document to start truncating from.
            // We will remove 'n' documents, so start truncating from the (n + 1)th document to the
            // end.
            auto exec = InternalPlanner::collectionScan(
                opCtx, fullNs.ns(), collection, PlanExecutor::NO_YIELD, InternalPlanner::BACKWARD);

            for (int i = 0; i < n + 1; ++i) {
                PlanExecutor::ExecState state = exec->getNext(nullptr, &end);
                if (PlanExecutor::ADVANCED != state) {
                    uasserted(ErrorCodes::IllegalOperation,
                              str::stream() << "invalid n, collection contains fewer than " << n
                                            << " documents");
                }
            }
        }

        BackgroundOperation::assertNoBgOpInProgForNs(fullNs.ns());
        IndexBuildsCoordinator::get(opCtx)->assertNoIndexBuildInProgForCollection(
            collection->uuid().get());

        collection->cappedTruncateAfter(opCtx, end, inc);

        return true;
    }
};

MONGO_REGISTER_TEST_COMMAND(CapTrunc);

// Testing-only, enabled via command line.
class EmptyCapped : public BasicCommand {
public:
    EmptyCapped() : BasicCommand("emptycapped") {}
    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kNever;
    }
    virtual bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }
    // No auth needed because it only works when enabled via command line.
    virtual void addRequiredPrivileges(const std::string& dbname,
                                       const BSONObj& cmdObj,
                                       std::vector<Privilege>* out) const {}

    virtual bool run(OperationContext* opCtx,
                     const string& dbname,
                     const BSONObj& cmdObj,
                     BSONObjBuilder& result) {
        const NamespaceString nss = CommandHelpers::parseNsCollectionRequired(dbname, cmdObj);

        uassertStatusOK(emptyCapped(opCtx, nss));
        return true;
    }
};

MONGO_REGISTER_TEST_COMMAND(EmptyCapped);
}
