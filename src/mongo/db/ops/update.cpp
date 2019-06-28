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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kWrite

#include "monger/platform/basic.h"

#include "monger/db/ops/update.h"

#include "monger/db/catalog/collection.h"
#include "monger/db/catalog/database.h"
#include "monger/db/catalog/database_holder.h"
#include "monger/db/client.h"
#include "monger/db/clientcursor.h"
#include "monger/db/concurrency/d_concurrency.h"
#include "monger/db/concurrency/write_conflict_exception.h"
#include "monger/db/exec/update_stage.h"
#include "monger/db/matcher/extensions_callback_real.h"
#include "monger/db/op_observer.h"
#include "monger/db/query/explain.h"
#include "monger/db/query/get_executor.h"
#include "monger/db/query/plan_summary_stats.h"
#include "monger/db/repl/repl_client_info.h"
#include "monger/db/repl/replication_coordinator.h"
#include "monger/db/update/update_driver.h"
#include "monger/db/update_index_data.h"
#include "monger/util/log.h"
#include "monger/util/scopeguard.h"

namespace monger {

UpdateResult update(OperationContext* opCtx, Database* db, const UpdateRequest& request) {
    invariant(db);

    // Explain should never use this helper.
    invariant(!request.isExplain());

    const NamespaceString& nsString = request.getNamespaceString();
    Collection* collection = db->getCollection(opCtx, nsString);

    // The update stage does not create its own collection.  As such, if the update is
    // an upsert, create the collection that the update stage inserts into beforehand.
    if (!collection && request.isUpsert()) {
        // We have to have an exclusive lock on the db to be allowed to create the collection.
        // Callers should either get an X or create the collection.
        const Locker* locker = opCtx->lockState();
        invariant(locker->isW() ||
                  locker->isLockHeldForMode(ResourceId(RESOURCE_DATABASE, nsString.db()), MODE_X));

        writeConflictRetry(opCtx, "createCollection", nsString.ns(), [&] {
            Lock::DBLock lk(opCtx, nsString.db(), MODE_X);

            const bool userInitiatedWritesAndNotPrimary = opCtx->writesAreReplicated() &&
                !repl::ReplicationCoordinator::get(opCtx)->canAcceptWritesFor(opCtx, nsString);

            if (userInitiatedWritesAndNotPrimary) {
                uassertStatusOK(Status(ErrorCodes::PrimarySteppedDown,
                                       str::stream() << "Not primary while creating collection "
                                                     << nsString
                                                     << " during upsert"));
            }
            WriteUnitOfWork wuow(opCtx);
            collection = db->createCollection(opCtx, nsString, CollectionOptions());
            invariant(collection);
            wuow.commit();
        });
    }

    // Parse the update, get an executor for it, run the executor, get stats out.
    const ExtensionsCallbackReal extensionsCallback(opCtx, &request.getNamespaceString());
    ParsedUpdate parsedUpdate(opCtx, &request, extensionsCallback);
    uassertStatusOK(parsedUpdate.parseRequest());

    OpDebug* const nullOpDebug = nullptr;
    auto exec = uassertStatusOK(getExecutorUpdate(opCtx, nullOpDebug, collection, &parsedUpdate));

    uassertStatusOK(exec->executePlan());

    const UpdateStats* updateStats = UpdateStage::getUpdateStats(exec.get());

    return UpdateStage::makeUpdateResult(updateStats);
}

BSONObj applyUpdateOperators(OperationContext* opCtx,
                             const BSONObj& from,
                             const BSONObj& operators) {
    const CollatorInterface* collator = nullptr;
    boost::intrusive_ptr<ExpressionContext> expCtx(new ExpressionContext(opCtx, collator));
    UpdateDriver driver(std::move(expCtx));
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    driver.parse(operators, arrayFilters);

    mutablebson::Document doc(from, mutablebson::Document::kInPlaceDisabled);

    const bool validateForStorage = false;
    const FieldRefSet emptyImmutablePaths;
    const bool isInsert = false;

    uassertStatusOK(
        driver.update(StringData(), &doc, validateForStorage, emptyImmutablePaths, isInsert));

    return doc.getObject();
}

}  // namespace monger
