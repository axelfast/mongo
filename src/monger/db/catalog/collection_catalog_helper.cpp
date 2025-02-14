/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include "monger/db/catalog/collection_catalog_helper.h"
#include "monger/db/catalog/collection.h"
#include "monger/db/catalog/collection_catalog.h"
#include "monger/db/catalog/collection_catalog_entry.h"
#include "monger/db/concurrency/d_concurrency.h"

namespace monger {

MONGO_FAIL_POINT_DEFINE(hangBeforeGettingNextCollection);

namespace catalog {

void forEachCollectionFromDb(OperationContext* opCtx,
                             StringData dbName,
                             LockMode collLockMode,
                             CollectionCatalog::CollectionInfoFn callback,
                             CollectionCatalog::CollectionInfoFn predicate) {

    CollectionCatalog& catalog = CollectionCatalog::get(opCtx);
    for (auto collectionIt = catalog.begin(dbName); collectionIt != catalog.end(); ++collectionIt) {
        auto uuid = collectionIt.uuid().get();
        if (predicate && !catalog.checkIfCollectionSatisfiable(uuid, predicate)) {
            continue;
        }

        auto nss = catalog.lookupNSSByUUID(uuid);

        // If the NamespaceString can't be resolved from the uuid, then the collection was dropped.
        if (!nss) {
            continue;
        }

        Lock::CollectionLock clk(opCtx, *nss, collLockMode);
        opCtx->recoveryUnit()->abandonSnapshot();

        auto collection = catalog.lookupCollectionByUUID(uuid);
        auto catalogEntry = catalog.lookupCollectionCatalogEntryByUUID(uuid);
        if (!collection || !catalogEntry || catalogEntry->ns() != *nss)
            continue;

        if (!callback(collection, catalogEntry))
            break;

        MONGO_FAIL_POINT_PAUSE_WHILE_SET(hangBeforeGettingNextCollection);
    }
}

}  // namespace catalog
}  // namespace monger
