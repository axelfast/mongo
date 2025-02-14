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

#pragma once

#include <memory>
#include <vector>

#include "monger/base/status.h"
#include "monger/bson/bsonobj.h"
#include "monger/db/clientcursor.h"
#include "monger/db/namespace_string.h"
#include "monger/db/repl/collection_cloner.h"
#include "monger/db/repl/storage_interface_mock.h"
#include "monger/db/service_context_test_fixture.h"
#include "monger/executor/network_interface_mock.h"
#include "monger/executor/thread_pool_task_executor_test_fixture.h"
#include "monger/stdx/condition_variable.h"
#include "monger/stdx/mutex.h"
#include "monger/util/concurrency/thread_pool.h"
#include "monger/util/net/hostandport.h"

namespace monger {

struct CollectionOptions;
class OperationContext;

namespace repl {

class BaseCloner;

class BaseClonerTest : public executor::ThreadPoolExecutorTest,
                       public ScopedGlobalServiceContextForTest {
public:
    typedef executor::NetworkInterfaceMock::NetworkOperationIterator NetworkOperationIterator;

    /**
     * Creates a count response with given document count.
     */
    static BSONObj createCountResponse(int documentCount);

    /**
     * Creates a cursor response with given array of documents.
     */
    static BSONObj createCursorResponse(CursorId cursorId,
                                        const std::string& ns,
                                        const BSONArray& docs,
                                        const char* batchFieldName);

    static BSONObj createCursorResponse(CursorId cursorId,
                                        const BSONArray& docs,
                                        const char* batchFieldName);

    static BSONObj createCursorResponse(CursorId cursorId, const BSONArray& docs);

    static BSONObj createFinalCursorResponse(const BSONArray& docs);

    /**
     * Creates a listCollections response with given array of index specs.
     */
    static BSONObj createListCollectionsResponse(CursorId cursorId,
                                                 const BSONArray& colls,
                                                 const char* batchFieldName);

    static BSONObj createListCollectionsResponse(CursorId cursorId, const BSONArray& colls);

    /**
     * Creates a listIndexes response with given array of index specs.
     */
    static BSONObj createListIndexesResponse(CursorId cursorId,
                                             const BSONArray& specs,
                                             const char* batchFieldName);

    static BSONObj createListIndexesResponse(CursorId cursorId, const BSONArray& specs);

    static const HostAndPort target;
    static const NamespaceString nss;
    static const BSONObj idIndexSpec;

    BaseClonerTest();

    virtual void clear();

    void setStatus(const Status& status);
    const Status& getStatus() const;

    void scheduleNetworkResponse(NetworkOperationIterator noi, const BSONObj& obj);
    void scheduleNetworkResponse(NetworkOperationIterator noi,
                                 ErrorCodes::Error code,
                                 const std::string& reason);
    void scheduleNetworkResponse(const BSONObj& obj);
    void scheduleNetworkResponse(ErrorCodes::Error code, const std::string& reason);
    void processNetworkResponse(const BSONObj& obj);
    void processNetworkResponse(ErrorCodes::Error code, const std::string& reason);
    void finishProcessingNetworkResponse();

    /**
     * Tests life cycle functionality.
     */
    virtual BaseCloner* getCloner() const = 0;
    void testLifeCycle();

protected:
    void setUp() override;
    void tearDown() override;

    std::unique_ptr<StorageInterfaceMock> storageInterface;
    std::unique_ptr<ThreadPool> dbWorkThreadPool;

private:
    // Protects member data of this base cloner fixture.
    mutable stdx::mutex _mutex;

    stdx::condition_variable _setStatusCondition;

    Status _status;
};

}  // namespace repl
}  // namespace monger
