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

#include "monger/db/catalog/collection.h"
#include "monger/db/catalog/index_catalog.h"
#include "monger/db/db_raii.h"
#include "monger/db/index/index_access_method.h"
#include "monger/db/index/index_descriptor.h"

#include "monger/db/stats/storage_stats.h"

namespace monger {

Status appendCollectionStorageStats(OperationContext* opCtx,
                                    const NamespaceString& nss,
                                    const BSONObj& param,
                                    BSONObjBuilder* result) {
    int scale = 1;
    if (param["scale"].isNumber()) {
        scale = param["scale"].numberInt();
        if (scale < 1) {
            return {ErrorCodes::BadValue, "scale has to be >= 1"};
        }
    } else if (param["scale"].trueValue()) {
        return {ErrorCodes::BadValue, "scale has to be a number >= 1"};
    }

    bool verbose = param["verbose"].trueValue();

    AutoGetCollectionForReadCommand ctx(opCtx, nss);
    Collection* collection = ctx.getCollection();  // Will be set if present
    if (!ctx.getDb() || !collection) {
        result->appendNumber("size", 0);
        result->appendNumber("count", 0);
        result->appendNumber("storageSize", 0);
        result->append("nindexes", 0);
        result->appendNumber("totalIndexSize", 0);
        result->append("indexDetails", BSONObj());
        result->append("indexSizes", BSONObj());
        result->append("scaleFactor", scale);
        std::string errmsg = !(ctx.getDb()) ? "Database [" + nss.db().toString() + "] not found."
                                            : "Collection [" + nss.toString() + "] not found.";
        return {ErrorCodes::NamespaceNotFound, errmsg};
    }

    long long size = collection->dataSize(opCtx) / scale;
    result->appendNumber("size", size);
    long long numRecords = collection->numRecords(opCtx);
    result->appendNumber("count", numRecords);

    if (numRecords)
        result->append("avgObjSize", collection->averageObjectSize(opCtx));

    RecordStore* recordStore = collection->getRecordStore();
    result->appendNumber(
        "storageSize",
        static_cast<long long>(recordStore->storageSize(opCtx, result, verbose ? 1 : 0)) / scale);

    recordStore->appendCustomStats(opCtx, result, scale);

    IndexCatalog* indexCatalog = collection->getIndexCatalog();
    result->append("nindexes", indexCatalog->numIndexesTotal(opCtx));

    BSONObjBuilder indexDetails;
    std::vector<std::string> indexBuilds;

    std::unique_ptr<IndexCatalog::IndexIterator> it =
        indexCatalog->getIndexIterator(opCtx, /*includeUnfinishedIndexes=*/true);
    while (it->more()) {
        const IndexCatalogEntry* entry = it->next();
        const IndexDescriptor* descriptor = entry->descriptor();
        const IndexAccessMethod* iam = entry->accessMethod();
        invariant(iam);

        BSONObjBuilder bob;
        if (iam->appendCustomStats(opCtx, &bob, scale)) {
            indexDetails.append(descriptor->indexName(), bob.obj());
        }

        if (!entry->isReady(opCtx)) {
            indexBuilds.push_back(descriptor->indexName());
        }
    }

    result->append("indexDetails", indexDetails.obj());
    result->append("indexBuilds", indexBuilds);

    BSONObjBuilder indexSizes;
    long long indexSize = collection->getIndexSize(opCtx, &indexSizes, scale);

    result->appendNumber("totalIndexSize", indexSize / scale);
    result->append("indexSizes", indexSizes.obj());
    result->append("scaleFactor", scale);

    return Status::OK();
}

Status appendCollectionRecordCount(OperationContext* opCtx,
                                   const NamespaceString& nss,
                                   BSONObjBuilder* result) {
    AutoGetCollectionForReadCommand ctx(opCtx, nss);
    if (!ctx.getDb()) {
        return {ErrorCodes::BadValue,
                str::stream() << "Database [" << nss.db().toString() << "] not found."};
    }

    Collection* collection = ctx.getCollection();
    if (!collection) {
        return {ErrorCodes::BadValue,
                str::stream() << "Collection [" << nss.toString() << "] not found."};
    }

    result->appendNumber("count", static_cast<long long>(collection->numRecords(opCtx)));

    return Status::OK();
}
}  // namespace monger
