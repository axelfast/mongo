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

#include <memory>

#include "monger/client/dbclient_cursor.h"
#include "monger/db/catalog/collection.h"
#include "monger/db/catalog/database.h"
#include "monger/db/catalog/index_catalog.h"
#include "monger/db/client.h"
#include "monger/db/db_raii.h"
#include "monger/db/dbdirectclient.h"
#include "monger/db/exec/fetch.h"
#include "monger/db/exec/index_scan.h"
#include "monger/db/exec/merge_sort.h"
#include "monger/db/exec/plan_stage.h"
#include "monger/db/exec/working_set_common.h"
#include "monger/db/json.h"
#include "monger/db/query/collation/collator_interface_mock.h"
#include "monger/db/query/plan_executor.h"
#include "monger/dbtests/dbtests.h"

/**
 * This file tests db/exec/merge_sort.cpp
 */

namespace QueryStageMergeSortTests {

using std::set;
using std::string;
using std::unique_ptr;
using std::make_unique;

class QueryStageMergeSortTestBase {
public:
    QueryStageMergeSortTestBase() : _client(&_opCtx) {}

    virtual ~QueryStageMergeSortTestBase() {
        dbtests::WriteContextForTests ctx(&_opCtx, ns());
        _client.dropCollection(ns());
    }

    void addIndex(const BSONObj& obj) {
        ASSERT_OK(dbtests::createIndex(&_opCtx, ns(), obj));
    }

    const IndexDescriptor* getIndex(const BSONObj& obj, Collection* coll) {
        std::vector<const IndexDescriptor*> indexes;
        coll->getIndexCatalog()->findIndexesByKeyPattern(&_opCtx, obj, false, &indexes);
        return indexes.empty() ? nullptr : indexes[0];
    }

    IndexScanParams makeIndexScanParams(OperationContext* opCtx,
                                        const IndexDescriptor* descriptor) {
        IndexScanParams params(opCtx, descriptor);
        params.bounds.isSimpleRange = true;
        params.bounds.startKey = objWithMinKey(1);
        params.bounds.endKey = objWithMaxKey(1);
        params.bounds.boundInclusion = BoundInclusion::kIncludeBothStartAndEndKeys;
        params.direction = 1;
        return params;
    }

    void insert(const BSONObj& obj) {
        _client.insert(ns(), obj);
    }

    void remove(const BSONObj& obj) {
        _client.remove(ns(), obj);
    }

    void update(const BSONObj& predicate, const BSONObj& update) {
        _client.update(ns(), predicate, update);
    }

    void getRecordIds(set<RecordId>* out, Collection* coll) {
        auto cursor = coll->getCursor(&_opCtx);
        while (auto record = cursor->next()) {
            out->insert(record->id);
        }
    }

    BSONObj objWithMinKey(int start) {
        BSONObjBuilder startKeyBob;
        startKeyBob.append("", start);
        startKeyBob.appendMinKey("");
        return startKeyBob.obj();
    }

    BSONObj objWithMaxKey(int start) {
        BSONObjBuilder endKeyBob;
        endKeyBob.append("", start);
        endKeyBob.appendMaxKey("");
        return endKeyBob.obj();
    }

    static const char* ns() {
        return "unittests.QueryStageMergeSort";
    }

    static NamespaceString nss() {
        return NamespaceString(ns());
    }

protected:
    const ServiceContext::UniqueOperationContext _txnPtr = cc().makeOperationContext();
    OperationContext& _opCtx = *_txnPtr;

private:
    DBDirectClient _client;
};

// SERVER-1205:
// find($or[{a:1}, {b:1}]).sort({c:1}) with indices {a:1, c:1} and {b:1, c:1}.
class QueryStageMergeSortPrefixIndex : public QueryStageMergeSortTestBase {
public:
    void run() {
        dbtests::WriteContextForTests ctx(&_opCtx, ns());
        Database* db = ctx.db();
        Collection* coll = db->getCollection(&_opCtx, nss());
        if (!coll) {
            WriteUnitOfWork wuow(&_opCtx);
            coll = db->createCollection(&_opCtx, nss());
            wuow.commit();
        }

        const int N = 50;

        for (int i = 0; i < N; ++i) {
            insert(BSON("a" << 1 << "c" << i));
            insert(BSON("b" << 1 << "c" << i));
        }

        BSONObj firstIndex = BSON("a" << 1 << "c" << 1);
        BSONObj secondIndex = BSON("b" << 1 << "c" << 1);

        addIndex(firstIndex);
        addIndex(secondIndex);

        unique_ptr<WorkingSet> ws = make_unique<WorkingSet>();
        // Sort by c:1
        MergeSortStageParams msparams;
        msparams.pattern = BSON("c" << 1);
        MergeSortStage* ms = new MergeSortStage(&_opCtx, msparams, ws.get());

        // a:1
        auto params = makeIndexScanParams(&_opCtx, getIndex(firstIndex, coll));
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));

        // b:1
        params = makeIndexScanParams(&_opCtx, getIndex(secondIndex, coll));
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));

        unique_ptr<FetchStage> fetchStage =
            make_unique<FetchStage>(&_opCtx, ws.get(), ms, nullptr, coll);
        // Must fetch if we want to easily pull out an obj.
        auto statusWithPlanExecutor = PlanExecutor::make(
            &_opCtx, std::move(ws), std::move(fetchStage), coll, PlanExecutor::NO_YIELD);
        ASSERT_OK(statusWithPlanExecutor.getStatus());
        auto exec = std::move(statusWithPlanExecutor.getValue());

        for (int i = 0; i < N; ++i) {
            BSONObj first, second;
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&first, nullptr));
            first = first.getOwned();
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&second, nullptr));
            ASSERT_EQUALS(first["c"].numberInt(), second["c"].numberInt());
            ASSERT_EQUALS(i, first["c"].numberInt());
            ASSERT((first.hasField("a") && second.hasField("b")) ||
                   (first.hasField("b") && second.hasField("a")));
        }

        // Should be done now.
        BSONObj foo;
        ASSERT_NOT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&foo, nullptr));
    }
};

// Each inserted document appears in both indices but is deduped and returned properly/sorted.
class QueryStageMergeSortDups : public QueryStageMergeSortTestBase {
public:
    void run() {
        dbtests::WriteContextForTests ctx(&_opCtx, ns());
        Database* db = ctx.db();
        Collection* coll = db->getCollection(&_opCtx, nss());
        if (!coll) {
            WriteUnitOfWork wuow(&_opCtx);
            coll = db->createCollection(&_opCtx, nss());
            wuow.commit();
        }

        const int N = 50;

        for (int i = 0; i < N; ++i) {
            insert(BSON("a" << 1 << "b" << 1 << "c" << i));
            insert(BSON("a" << 1 << "b" << 1 << "c" << i));
        }

        BSONObj firstIndex = BSON("a" << 1 << "c" << 1);
        BSONObj secondIndex = BSON("b" << 1 << "c" << 1);

        addIndex(firstIndex);
        addIndex(secondIndex);

        unique_ptr<WorkingSet> ws = make_unique<WorkingSet>();
        // Sort by c:1
        MergeSortStageParams msparams;
        msparams.pattern = BSON("c" << 1);
        MergeSortStage* ms = new MergeSortStage(&_opCtx, msparams, ws.get());

        // a:1
        auto params = makeIndexScanParams(&_opCtx, getIndex(firstIndex, coll));
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));

        // b:1
        params = makeIndexScanParams(&_opCtx, getIndex(secondIndex, coll));
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));
        unique_ptr<FetchStage> fetchStage =
            make_unique<FetchStage>(&_opCtx, ws.get(), ms, nullptr, coll);

        auto statusWithPlanExecutor = PlanExecutor::make(
            &_opCtx, std::move(ws), std::move(fetchStage), coll, PlanExecutor::NO_YIELD);
        ASSERT_OK(statusWithPlanExecutor.getStatus());
        auto exec = std::move(statusWithPlanExecutor.getValue());

        for (int i = 0; i < N; ++i) {
            BSONObj first, second;
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&first, nullptr));
            first = first.getOwned();
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&second, nullptr));
            ASSERT_EQUALS(first["c"].numberInt(), second["c"].numberInt());
            ASSERT_EQUALS(i, first["c"].numberInt());
            ASSERT((first.hasField("a") && second.hasField("b")) ||
                   (first.hasField("b") && second.hasField("a")));
        }

        // Should be done now.
        BSONObj foo;
        ASSERT_EQUALS(PlanExecutor::IS_EOF, exec->getNext(&foo, nullptr));
    }
};

// Each inserted document appears in both indices, no deduping, get each result twice.
class QueryStageMergeSortDupsNoDedup : public QueryStageMergeSortTestBase {
public:
    void run() {
        dbtests::WriteContextForTests ctx(&_opCtx, ns());
        Database* db = ctx.db();
        Collection* coll = db->getCollection(&_opCtx, nss());
        if (!coll) {
            WriteUnitOfWork wuow(&_opCtx);
            coll = db->createCollection(&_opCtx, nss());
            wuow.commit();
        }

        const int N = 50;

        for (int i = 0; i < N; ++i) {
            insert(BSON("a" << 1 << "b" << 1 << "c" << i));
        }

        BSONObj firstIndex = BSON("a" << 1 << "c" << 1);
        BSONObj secondIndex = BSON("b" << 1 << "c" << 1);

        addIndex(firstIndex);
        addIndex(secondIndex);

        unique_ptr<WorkingSet> ws = make_unique<WorkingSet>();
        // Sort by c:1
        MergeSortStageParams msparams;
        msparams.dedup = false;
        msparams.pattern = BSON("c" << 1);
        MergeSortStage* ms = new MergeSortStage(&_opCtx, msparams, ws.get());

        // a:1
        auto params = makeIndexScanParams(&_opCtx, getIndex(firstIndex, coll));
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));

        // b:1
        params = makeIndexScanParams(&_opCtx, getIndex(secondIndex, coll));
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));
        unique_ptr<FetchStage> fetchStage =
            make_unique<FetchStage>(&_opCtx, ws.get(), ms, nullptr, coll);

        auto statusWithPlanExecutor = PlanExecutor::make(
            &_opCtx, std::move(ws), std::move(fetchStage), coll, PlanExecutor::NO_YIELD);
        ASSERT_OK(statusWithPlanExecutor.getStatus());
        auto exec = std::move(statusWithPlanExecutor.getValue());

        for (int i = 0; i < N; ++i) {
            BSONObj first, second;
            // We inserted N objects but we get 2 * N from the runner because of dups.
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&first, nullptr));
            first = first.getOwned();
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&second, nullptr));
            ASSERT_EQUALS(first["c"].numberInt(), second["c"].numberInt());
            ASSERT_EQUALS(i, first["c"].numberInt());
            ASSERT((first.hasField("a") && second.hasField("b")) ||
                   (first.hasField("b") && second.hasField("a")));
        }

        // Should be done now.
        BSONObj foo;
        ASSERT_EQUALS(PlanExecutor::IS_EOF, exec->getNext(&foo, nullptr));
    }
};

// Decreasing indices merged ok.  Basically the test above but decreasing.
class QueryStageMergeSortPrefixIndexReverse : public QueryStageMergeSortTestBase {
public:
    void run() {
        dbtests::WriteContextForTests ctx(&_opCtx, ns());
        Database* db = ctx.db();
        Collection* coll = db->getCollection(&_opCtx, nss());
        if (!coll) {
            WriteUnitOfWork wuow(&_opCtx);
            coll = db->createCollection(&_opCtx, nss());
            wuow.commit();
        }

        const int N = 50;

        for (int i = 0; i < N; ++i) {
            // We insert a:1 c:i for i=0..49 but in reverse order for the heck of it.
            insert(BSON("a" << 1 << "c" << N - i - 1));
            insert(BSON("b" << 1 << "c" << i));
        }

        BSONObj firstIndex = BSON("a" << 1 << "c" << -1);
        BSONObj secondIndex = BSON("b" << 1 << "c" << -1);

        addIndex(firstIndex);
        addIndex(secondIndex);

        unique_ptr<WorkingSet> ws = make_unique<WorkingSet>();
        // Sort by c:-1
        MergeSortStageParams msparams;
        msparams.pattern = BSON("c" << -1);
        MergeSortStage* ms = new MergeSortStage(&_opCtx, msparams, ws.get());

        // a:1
        auto params = makeIndexScanParams(&_opCtx, getIndex(firstIndex, coll));
        params.bounds.startKey = objWithMaxKey(1);
        params.bounds.endKey = objWithMinKey(1);
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));

        // b:1
        params = makeIndexScanParams(&_opCtx, getIndex(secondIndex, coll));
        params.bounds.startKey = objWithMaxKey(1);
        params.bounds.endKey = objWithMinKey(1);
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));
        unique_ptr<FetchStage> fetchStage =
            make_unique<FetchStage>(&_opCtx, ws.get(), ms, nullptr, coll);

        auto statusWithPlanExecutor = PlanExecutor::make(
            &_opCtx, std::move(ws), std::move(fetchStage), coll, PlanExecutor::NO_YIELD);
        ASSERT_OK(statusWithPlanExecutor.getStatus());
        auto exec = std::move(statusWithPlanExecutor.getValue());

        for (int i = 0; i < N; ++i) {
            BSONObj first, second;
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&first, nullptr));
            first = first.getOwned();
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&second, nullptr));
            ASSERT_EQUALS(first["c"].numberInt(), second["c"].numberInt());
            ASSERT_EQUALS(N - i - 1, first["c"].numberInt());
            ASSERT((first.hasField("a") && second.hasField("b")) ||
                   (first.hasField("b") && second.hasField("a")));
        }

        // Should be done now.
        BSONObj foo;
        ASSERT_EQUALS(PlanExecutor::IS_EOF, exec->getNext(&foo, nullptr));
    }
};

// One stage EOF immediately
class QueryStageMergeSortOneStageEOF : public QueryStageMergeSortTestBase {
public:
    void run() {
        dbtests::WriteContextForTests ctx(&_opCtx, ns());
        Database* db = ctx.db();
        Collection* coll = db->getCollection(&_opCtx, nss());
        if (!coll) {
            WriteUnitOfWork wuow(&_opCtx);
            coll = db->createCollection(&_opCtx, nss());
            wuow.commit();
        }

        const int N = 50;

        for (int i = 0; i < N; ++i) {
            insert(BSON("a" << 1 << "c" << i));
            insert(BSON("b" << 1 << "c" << i));
        }

        BSONObj firstIndex = BSON("a" << 1 << "c" << 1);
        BSONObj secondIndex = BSON("b" << 1 << "c" << 1);

        addIndex(firstIndex);
        addIndex(secondIndex);

        unique_ptr<WorkingSet> ws = make_unique<WorkingSet>();
        // Sort by c:1
        MergeSortStageParams msparams;
        msparams.pattern = BSON("c" << 1);
        MergeSortStage* ms = new MergeSortStage(&_opCtx, msparams, ws.get());

        // a:1
        auto params = makeIndexScanParams(&_opCtx, getIndex(firstIndex, coll));
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));

        // b:51 (EOF)
        params = makeIndexScanParams(&_opCtx, getIndex(secondIndex, coll));
        params.bounds.startKey = BSON("" << 51 << "" << MinKey);
        params.bounds.endKey = BSON("" << 51 << "" << MaxKey);
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));
        unique_ptr<FetchStage> fetchStage =
            make_unique<FetchStage>(&_opCtx, ws.get(), ms, nullptr, coll);

        auto statusWithPlanExecutor = PlanExecutor::make(
            &_opCtx, std::move(ws), std::move(fetchStage), coll, PlanExecutor::NO_YIELD);
        ASSERT_OK(statusWithPlanExecutor.getStatus());
        auto exec = std::move(statusWithPlanExecutor.getValue());

        // Only getting results from the a:1 index scan.
        for (int i = 0; i < N; ++i) {
            BSONObj obj;
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&obj, nullptr));
            ASSERT_EQUALS(i, obj["c"].numberInt());
            ASSERT_EQUALS(1, obj["a"].numberInt());
        }

        // Should be done now.
        BSONObj foo;
        ASSERT_EQUALS(PlanExecutor::IS_EOF, exec->getNext(&foo, nullptr));
    }
};

// N stages each have 1 result
class QueryStageMergeSortManyShort : public QueryStageMergeSortTestBase {
public:
    void run() {
        dbtests::WriteContextForTests ctx(&_opCtx, ns());
        Database* db = ctx.db();
        Collection* coll = db->getCollection(&_opCtx, nss());
        if (!coll) {
            WriteUnitOfWork wuow(&_opCtx);
            coll = db->createCollection(&_opCtx, nss());
            wuow.commit();
        }

        unique_ptr<WorkingSet> ws = make_unique<WorkingSet>();
        // Sort by foo:1
        MergeSortStageParams msparams;
        msparams.pattern = BSON("foo" << 1);
        MergeSortStage* ms = new MergeSortStage(&_opCtx, msparams, ws.get());

        int numIndices = 20;
        for (int i = 0; i < numIndices; ++i) {
            // 'a', 'b', ...
            string index(1, 'a' + i);
            insert(BSON(index << 1 << "foo" << i));

            BSONObj indexSpec = BSON(index << 1 << "foo" << 1);
            addIndex(indexSpec);
            auto params = makeIndexScanParams(&_opCtx, getIndex(indexSpec, coll));
            ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));
        }
        unique_ptr<FetchStage> fetchStage =
            make_unique<FetchStage>(&_opCtx, ws.get(), ms, nullptr, coll);

        auto statusWithPlanExecutor = PlanExecutor::make(
            &_opCtx, std::move(ws), std::move(fetchStage), coll, PlanExecutor::NO_YIELD);
        ASSERT_OK(statusWithPlanExecutor.getStatus());
        auto exec = std::move(statusWithPlanExecutor.getValue());

        for (int i = 0; i < numIndices; ++i) {
            BSONObj obj;
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&obj, nullptr));
            ASSERT_EQUALS(i, obj["foo"].numberInt());
            string index(1, 'a' + i);
            ASSERT_EQUALS(1, obj[index].numberInt());
        }

        // Should be done now.
        BSONObj foo;
        ASSERT_EQUALS(PlanExecutor::IS_EOF, exec->getNext(&foo, nullptr));
    }
};

// Document is deleted mid-run.
class QueryStageMergeSortDeletedDocument : public QueryStageMergeSortTestBase {
public:
    void run() {
        dbtests::WriteContextForTests ctx(&_opCtx, ns());
        Database* db = ctx.db();
        Collection* coll = db->getCollection(&_opCtx, nss());
        if (!coll) {
            WriteUnitOfWork wuow(&_opCtx);
            coll = db->createCollection(&_opCtx, nss());
            wuow.commit();
        }

        WorkingSet ws;
        // Sort by foo:1
        MergeSortStageParams msparams;
        msparams.pattern = BSON("foo" << 1);
        auto ms = make_unique<MergeSortStage>(&_opCtx, msparams, &ws);

        // Index 'a'+i has foo equal to 'i'.

        int numIndices = 20;
        for (int i = 0; i < numIndices; ++i) {
            // 'a', 'b', ...
            string index(1, 'a' + i);
            insert(BSON(index << 1 << "foo" << i));

            BSONObj indexSpec = BSON(index << 1 << "foo" << 1);
            addIndex(indexSpec);
            auto params = makeIndexScanParams(&_opCtx, getIndex(indexSpec, coll));
            ms->addChild(new IndexScan(&_opCtx, params, &ws, nullptr));
        }

        set<RecordId> recordIds;
        getRecordIds(&recordIds, coll);

        set<RecordId>::iterator it = recordIds.begin();

        // Get 10 results.  Should be getting results in order of 'recordIds'.
        int count = 0;
        while (!ms->isEOF() && count < 10) {
            WorkingSetID id = WorkingSet::INVALID_ID;
            PlanStage::StageState status = ms->work(&id);
            if (PlanStage::ADVANCED != status) {
                continue;
            }

            WorkingSetMember* member = ws.get(id);
            ASSERT_EQUALS(member->recordId, *it);
            BSONElement elt;
            string index(1, 'a' + count);
            ASSERT(member->getFieldDotted(index, &elt));
            ASSERT_EQUALS(1, elt.numberInt());
            ASSERT(member->getFieldDotted("foo", &elt));
            ASSERT_EQUALS(count, elt.numberInt());
            ++count;
            ++it;
        }

        // Delete recordIds[11]. The deleted document should be buffered inside the SORT_MERGE
        // stage, and therefore should still be returned.
        ms->saveState();
        remove(BSON(std::string(1u, 'a' + count) << 1));
        ms->restoreState();

        // Make sure recordIds[11] is returned as expected. We expect the corresponding working set
        // member to remain in RID_AND_IDX state. It should have been marked as "suspicious", since
        // this is an index key WSM that survived a yield.
        {
            WorkingSetID id = WorkingSet::INVALID_ID;
            PlanStage::StageState status;
            do {
                status = ms->work(&id);
            } while (PlanStage::ADVANCED != status);

            WorkingSetMember* member = ws.get(id);
            ASSERT_EQ(member->getState(), WorkingSetMember::RID_AND_IDX);
            ASSERT(member->hasRecordId());
            ASSERT(!member->hasObj());
            string index(1, 'a' + count);
            BSONElement elt;
            ASSERT_TRUE(member->getFieldDotted(index, &elt));
            ASSERT_EQUALS(1, elt.numberInt());
            ASSERT(member->getFieldDotted("foo", &elt));
            ASSERT_EQUALS(count, elt.numberInt());

            // An attempt to fetch the WSM should show that the key is no longer present in the
            // index.
            ASSERT_FALSE(WorkingSetCommon::fetch(&_opCtx, &ws, id, coll->getCursor(&_opCtx)));

            ++it;
            ++count;
        }

        // And get the rest.
        while (!ms->isEOF()) {
            WorkingSetID id = WorkingSet::INVALID_ID;
            PlanStage::StageState status = ms->work(&id);
            if (PlanStage::ADVANCED != status) {
                continue;
            }

            WorkingSetMember* member = ws.get(id);
            ASSERT_EQUALS(member->recordId, *it);
            BSONElement elt;
            string index(1, 'a' + count);
            ASSERT_TRUE(member->getFieldDotted(index, &elt));
            ASSERT_EQUALS(1, elt.numberInt());
            ASSERT(member->getFieldDotted("foo", &elt));
            ASSERT_EQUALS(count, elt.numberInt());
            ++count;
            ++it;
        }
    }
};

// Test that if a WSM buffered inside the merge sort stage gets updated, we return the document and
// then correctly dedup if we see the same RecordId again.
class QueryStageMergeSortConcurrentUpdateDedup : public QueryStageMergeSortTestBase {
public:
    void run() {
        dbtests::WriteContextForTests ctx(&_opCtx, ns());
        Database* db = ctx.db();
        Collection* coll = db->getCollection(&_opCtx, nss());
        if (!coll) {
            WriteUnitOfWork wuow(&_opCtx);
            coll = db->createCollection(&_opCtx, nss());
            wuow.commit();
        }

        // Insert data.
        insert(BSON("_id" << 4 << "a" << 4));
        insert(BSON("_id" << 5 << "a" << 5));
        insert(BSON("_id" << 6 << "a" << 6));

        addIndex(BSON("a" << 1));

        std::set<RecordId> rids;
        getRecordIds(&rids, coll);
        set<RecordId>::iterator it = rids.begin();

        WorkingSet ws;
        WorkingSetMember* member;
        MergeSortStageParams msparams;
        msparams.pattern = BSON("a" << 1);
        auto ms = std::make_unique<MergeSortStage>(&_opCtx, msparams, &ws);

        // First child scans [5, 10].
        {
            auto params = makeIndexScanParams(&_opCtx, getIndex(BSON("a" << 1), coll));
            params.bounds.startKey = BSON("" << 5);
            params.bounds.endKey = BSON("" << 10);
            auto fetchStage = std::make_unique<FetchStage>(
                &_opCtx, &ws, new IndexScan(&_opCtx, params, &ws, nullptr), nullptr, coll);
            ms->addChild(fetchStage.release());
        }

        // Second child scans [4, 10].
        {
            auto params = makeIndexScanParams(&_opCtx, getIndex(BSON("a" << 1), coll));
            params.bounds.startKey = BSON("" << 4);
            params.bounds.endKey = BSON("" << 10);
            auto fetchStage = std::make_unique<FetchStage>(
                &_opCtx, &ws, new IndexScan(&_opCtx, params, &ws, nullptr), nullptr, coll);
            ms->addChild(fetchStage.release());
        }

        // First doc should be {a: 4}.
        member = getNextResult(&ws, ms.get());
        ASSERT_EQ(member->getState(), WorkingSetMember::RID_AND_OBJ);
        ASSERT_EQ(member->recordId, *it);
        ASSERT_BSONOBJ_EQ(member->obj.value(), BSON("_id" << 4 << "a" << 4));
        ++it;

        // Update doc {a: 5} such that the postimage will no longer match the query.
        ms->saveState();
        update(BSON("a" << 5), BSON("$set" << BSON("a" << 15)));
        ms->restoreState();

        // Invalidated doc {a: 5} should still get returned. We expect an RID_AND_OBJ working set
        // member with an owned BSONObj.
        member = getNextResult(&ws, ms.get());
        ASSERT_EQ(member->getState(), WorkingSetMember::RID_AND_OBJ);
        ASSERT(member->hasObj());
        ASSERT(member->obj.value().isOwned());
        ASSERT_BSONOBJ_EQ(member->obj.value(), BSON("_id" << 5 << "a" << 5));
        ++it;

        // We correctly dedup the invalidated doc and return {a: 6} next.
        member = getNextResult(&ws, ms.get());
        ASSERT_EQ(member->getState(), WorkingSetMember::RID_AND_OBJ);
        ASSERT_EQ(member->recordId, *it);
        ASSERT_BSONOBJ_EQ(member->obj.value(), BSON("_id" << 6 << "a" << 6));
    }

private:
    WorkingSetMember* getNextResult(WorkingSet* ws, PlanStage* stage) {
        while (!stage->isEOF()) {
            WorkingSetID id = WorkingSet::INVALID_ID;
            PlanStage::StageState status = stage->work(&id);
            if (PlanStage::ADVANCED != status) {
                continue;
            }

            return ws->get(id);
        }

        FAIL("Expected to produce another result but hit EOF");
        return nullptr;
    }
};

class QueryStageMergeSortStringsWithNullCollation : public QueryStageMergeSortTestBase {
public:
    void run() {
        dbtests::WriteContextForTests ctx(&_opCtx, ns());
        Database* db = ctx.db();
        Collection* coll = db->getCollection(&_opCtx, nss());
        if (!coll) {
            WriteUnitOfWork wuow(&_opCtx);
            coll = db->createCollection(&_opCtx, nss());
            wuow.commit();
        }

        const int N = 50;

        for (int i = 0; i < N; ++i) {
            insert(BSON("a" << 1 << "c" << i << "d"
                            << "abc"));
            insert(BSON("b" << 1 << "c" << i << "d"
                            << "cba"));
        }

        BSONObj firstIndex = BSON("a" << 1 << "c" << 1 << "d" << 1);
        BSONObj secondIndex = BSON("b" << 1 << "c" << 1 << "d" << 1);

        addIndex(firstIndex);
        addIndex(secondIndex);

        unique_ptr<WorkingSet> ws = make_unique<WorkingSet>();
        // Sort by c:1, d:1
        MergeSortStageParams msparams;
        msparams.pattern = BSON("c" << 1 << "d" << 1);
        msparams.collator = nullptr;
        MergeSortStage* ms = new MergeSortStage(&_opCtx, msparams, ws.get());

        // a:1
        auto params = makeIndexScanParams(&_opCtx, getIndex(firstIndex, coll));
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));

        // b:1
        params = makeIndexScanParams(&_opCtx, getIndex(secondIndex, coll));
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));

        unique_ptr<FetchStage> fetchStage =
            make_unique<FetchStage>(&_opCtx, ws.get(), ms, nullptr, coll);
        // Must fetch if we want to easily pull out an obj.
        auto statusWithPlanExecutor = PlanExecutor::make(
            &_opCtx, std::move(ws), std::move(fetchStage), coll, PlanExecutor::NO_YIELD);
        ASSERT_OK(statusWithPlanExecutor.getStatus());
        auto exec = std::move(statusWithPlanExecutor.getValue());

        for (int i = 0; i < N; ++i) {
            BSONObj first, second;
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&first, nullptr));
            first = first.getOwned();
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&second, nullptr));
            ASSERT_EQUALS(first["c"].numberInt(), second["c"].numberInt());
            ASSERT_EQUALS(i, first["c"].numberInt());
            // {a: 1, c: i, d: "abc"} should precede {b: 1, c: i, d: "bca"}.
            ASSERT(first.hasField("a") && second.hasField("b"));
        }

        // Should be done now.
        BSONObj foo;
        ASSERT_NOT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&foo, nullptr));
    }
};

class QueryStageMergeSortStringsRespectsCollation : public QueryStageMergeSortTestBase {
public:
    void run() {
        dbtests::WriteContextForTests ctx(&_opCtx, ns());
        Database* db = ctx.db();
        Collection* coll = db->getCollection(&_opCtx, nss());
        if (!coll) {
            WriteUnitOfWork wuow(&_opCtx);
            coll = db->createCollection(&_opCtx, nss());
            wuow.commit();
        }

        const int N = 50;

        for (int i = 0; i < N; ++i) {
            insert(BSON("a" << 1 << "c" << i << "d"
                            << "abc"));
            insert(BSON("b" << 1 << "c" << i << "d"
                            << "cba"));
        }

        BSONObj firstIndex = BSON("a" << 1 << "c" << 1 << "d" << 1);
        BSONObj secondIndex = BSON("b" << 1 << "c" << 1 << "d" << 1);

        addIndex(firstIndex);
        addIndex(secondIndex);

        unique_ptr<WorkingSet> ws = make_unique<WorkingSet>();
        // Sort by c:1, d:1
        MergeSortStageParams msparams;
        msparams.pattern = BSON("c" << 1 << "d" << 1);
        CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);
        msparams.collator = &collator;
        MergeSortStage* ms = new MergeSortStage(&_opCtx, msparams, ws.get());

        // a:1
        auto params = makeIndexScanParams(&_opCtx, getIndex(firstIndex, coll));
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));

        // b:1
        params = makeIndexScanParams(&_opCtx, getIndex(secondIndex, coll));
        ms->addChild(new IndexScan(&_opCtx, params, ws.get(), nullptr));

        unique_ptr<FetchStage> fetchStage =
            make_unique<FetchStage>(&_opCtx, ws.get(), ms, nullptr, coll);
        // Must fetch if we want to easily pull out an obj.
        auto statusWithPlanExecutor = PlanExecutor::make(
            &_opCtx, std::move(ws), std::move(fetchStage), coll, PlanExecutor::NO_YIELD);
        ASSERT_OK(statusWithPlanExecutor.getStatus());
        auto exec = std::move(statusWithPlanExecutor.getValue());

        for (int i = 0; i < N; ++i) {
            BSONObj first, second;
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&first, nullptr));
            first = first.getOwned();
            ASSERT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&second, nullptr));
            ASSERT_EQUALS(first["c"].numberInt(), second["c"].numberInt());
            ASSERT_EQUALS(i, first["c"].numberInt());
            // {b: 1, c: i, d: "cba"} should precede {a: 1, c: i, d: "abc"}.
            ASSERT(first.hasField("b") && second.hasField("a"));
        }

        // Should be done now.
        BSONObj foo;
        ASSERT_NOT_EQUALS(PlanExecutor::ADVANCED, exec->getNext(&foo, nullptr));
    }
};

class All : public Suite {
public:
    All() : Suite("query_stage_merge_sort_test") {}

    void setupTests() {
        add<QueryStageMergeSortPrefixIndex>();
        add<QueryStageMergeSortDups>();
        add<QueryStageMergeSortDupsNoDedup>();
        add<QueryStageMergeSortPrefixIndexReverse>();
        add<QueryStageMergeSortOneStageEOF>();
        add<QueryStageMergeSortManyShort>();
        add<QueryStageMergeSortDeletedDocument>();
        add<QueryStageMergeSortConcurrentUpdateDedup>();
        add<QueryStageMergeSortStringsWithNullCollation>();
        add<QueryStageMergeSortStringsRespectsCollation>();
    }
};

SuiteInstance<All> queryStageMergeSortTest;

}  // namespace
