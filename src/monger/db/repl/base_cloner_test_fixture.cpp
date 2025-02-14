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

#include "monger/db/repl/base_cloner_test_fixture.h"

#include <memory>

#include "monger/db/jsobj.h"
#include "monger/stdx/thread.h"
#include "monger/util/str.h"

namespace monger {
namespace repl {
using executor::RemoteCommandRequest;
using executor::RemoteCommandResponse;
using namespace unittest;

const HostAndPort BaseClonerTest::target("localhost", -1);
const NamespaceString BaseClonerTest::nss("db.coll");
const BSONObj BaseClonerTest::idIndexSpec = BSON("v" << 1 << "key" << BSON("_id" << 1) << "name"
                                                     << "_id_"
                                                     << "ns"
                                                     << nss.ns());

// static
BSONObj BaseClonerTest::createCountResponse(int documentCount) {
    return BSON("n" << documentCount << "ok" << 1);
}

// static
BSONObj BaseClonerTest::createCursorResponse(CursorId cursorId,
                                             const std::string& ns,
                                             const BSONArray& docs,
                                             const char* batchFieldName) {
    return BSON("cursor" << BSON("id" << cursorId << "ns" << ns << batchFieldName << docs) << "ok"
                         << 1);
}

// static
BSONObj BaseClonerTest::createCursorResponse(CursorId cursorId,
                                             const BSONArray& docs,
                                             const char* batchFieldName) {
    return createCursorResponse(cursorId, nss.toString(), docs, batchFieldName);
}

// static
BSONObj BaseClonerTest::createCursorResponse(CursorId cursorId, const BSONArray& docs) {
    return createCursorResponse(cursorId, docs, "firstBatch");
}

// static
BSONObj BaseClonerTest::createFinalCursorResponse(const BSONArray& docs) {
    return createCursorResponse(0, docs, "nextBatch");
}

// static
BSONObj BaseClonerTest::createListCollectionsResponse(CursorId cursorId,
                                                      const BSONArray& colls,
                                                      const char* fieldName) {
    return createCursorResponse(cursorId, "test.$cmd.listCollections.coll", colls, fieldName);
}

// static
BSONObj BaseClonerTest::createListCollectionsResponse(CursorId cursorId, const BSONArray& colls) {
    return createListCollectionsResponse(cursorId, colls, "firstBatch");
}

// static
BSONObj BaseClonerTest::createListIndexesResponse(CursorId cursorId,
                                                  const BSONArray& specs,
                                                  const char* batchFieldName) {
    return createCursorResponse(cursorId, "test.coll", specs, batchFieldName);
}

// static
BSONObj BaseClonerTest::createListIndexesResponse(CursorId cursorId, const BSONArray& specs) {
    return createListIndexesResponse(cursorId, specs, "firstBatch");
}

namespace {
struct EnsureClientHasBeenInitialized : public executor::ThreadPoolMock::Options {
    EnsureClientHasBeenInitialized() : executor::ThreadPoolMock::Options() {
        onCreateThread = []() { Client::initThread("CollectionClonerTestThread"); };
    }
};
}  // namespace

BaseClonerTest::BaseClonerTest()
    : ThreadPoolExecutorTest(EnsureClientHasBeenInitialized()),
      _mutex(),
      _setStatusCondition(),
      _status(getDetectableErrorStatus()) {}

void BaseClonerTest::setUp() {
    executor::ThreadPoolExecutorTest::setUp();
    clear();
    launchExecutorThread();

    Client::initThread("CollectionClonerTest");
    ThreadPool::Options options;
    options.minThreads = 1U;
    options.maxThreads = 1U;
    options.onCreateThread = [](StringData threadName) { Client::initThread(threadName); };
    dbWorkThreadPool = std::make_unique<ThreadPool>(options);
    dbWorkThreadPool->startup();

    storageInterface.reset(new StorageInterfaceMock());
}

void BaseClonerTest::tearDown() {
    getExecutor().shutdown();
    getExecutor().join();

    storageInterface.reset();

    dbWorkThreadPool.reset();
    Client::releaseCurrent();
}

void BaseClonerTest::clear() {
    _status = getDetectableErrorStatus();
}

void BaseClonerTest::setStatus(const Status& status) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    _status = status;
    _setStatusCondition.notify_all();
}

const Status& BaseClonerTest::getStatus() const {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    return _status;
}

void BaseClonerTest::scheduleNetworkResponse(NetworkOperationIterator noi, const BSONObj& obj) {
    auto net = getNet();
    Milliseconds millis(0);
    RemoteCommandResponse response(obj, millis);
    log() << "Scheduling response to request:" << noi->getDiagnosticString() << " -- resp:" << obj;
    net->scheduleResponse(noi, net->now(), response);
}

void BaseClonerTest::scheduleNetworkResponse(NetworkOperationIterator noi,
                                             ErrorCodes::Error code,
                                             const std::string& reason) {
    auto net = getNet();
    RemoteCommandResponse responseStatus(code, reason);
    log() << "Scheduling error response to request:" << noi->getDiagnosticString()
          << " -- status:" << responseStatus.status.toString();
    net->scheduleResponse(noi, net->now(), responseStatus);
}

void BaseClonerTest::scheduleNetworkResponse(const BSONObj& obj) {
    if (!getNet()->hasReadyRequests()) {
        BSONObjBuilder b;
        getExecutor().appendDiagnosticBSON(&b);
        log() << "Expected network request for resp: " << obj;
        log() << "      replExec: " << b.done();
        log() << "      net:" << getNet()->getDiagnosticString();
    }
    if (getStatus() != getDetectableErrorStatus()) {
        log() << "Status has changed during network response playback to: " << getStatus();
        return;
    }
    ASSERT_TRUE(getNet()->hasReadyRequests());
    scheduleNetworkResponse(getNet()->getNextReadyRequest(), obj);
}

void BaseClonerTest::scheduleNetworkResponse(ErrorCodes::Error code, const std::string& reason) {
    ASSERT_TRUE(getNet()->hasReadyRequests());
    scheduleNetworkResponse(getNet()->getNextReadyRequest(), code, reason);
}

void BaseClonerTest::processNetworkResponse(const BSONObj& obj) {
    scheduleNetworkResponse(obj);
    finishProcessingNetworkResponse();
}

void BaseClonerTest::processNetworkResponse(ErrorCodes::Error code, const std::string& reason) {
    scheduleNetworkResponse(code, reason);
    finishProcessingNetworkResponse();
}

void BaseClonerTest::finishProcessingNetworkResponse() {
    clear();
    getNet()->runReadyNetworkOperations();
}

void BaseClonerTest::testLifeCycle() {
    log() << "Testing IsActiveAfterStart";
    ASSERT_FALSE(getCloner()->isActive());
    ASSERT_OK(getCloner()->startup());
    ASSERT_TRUE(getCloner()->isActive());
    tearDown();

    log() << "Testing StartWhenActive";
    setUp();
    ASSERT_OK(getCloner()->startup());
    ASSERT_TRUE(getCloner()->isActive());
    ASSERT_NOT_OK(getCloner()->startup());
    ASSERT_TRUE(getCloner()->isActive());
    tearDown();

    log() << "Testing CancelWithoutStart";
    setUp();
    ASSERT_FALSE(getCloner()->isActive());
    getCloner()->shutdown();
    ASSERT_FALSE(getCloner()->isActive());
    tearDown();

    log() << "Testing WaitWithoutStart";
    setUp();
    ASSERT_FALSE(getCloner()->isActive());
    getCloner()->join();
    ASSERT_FALSE(getCloner()->isActive());
    tearDown();

    log() << "Testing ShutdownBeforeStart";
    setUp();
    getExecutor().shutdown();
    ASSERT_NOT_OK(getCloner()->startup());
    ASSERT_FALSE(getCloner()->isActive());
    tearDown();

    log() << "Testing StartAndCancel";
    setUp();
    ASSERT_OK(getCloner()->startup());
    getCloner()->shutdown();
    {
        executor::NetworkInterfaceMock::InNetworkGuard guard(getNet());
        finishProcessingNetworkResponse();
    }
    ASSERT_EQUALS(ErrorCodes::CallbackCanceled, getStatus().code());
    ASSERT_FALSE(getCloner()->isActive());
    tearDown();

    log() << "Testing StartButShutdown";
    setUp();
    ASSERT_OK(getCloner()->startup());
    {
        executor::NetworkInterfaceMock::InNetworkGuard guard(getNet());
        scheduleNetworkResponse(BSON("ok" << 1));
        // Network interface should not deliver mock response to callback.
        getExecutor().shutdown();
        finishProcessingNetworkResponse();
    }
    ASSERT_EQUALS(ErrorCodes::CallbackCanceled, getStatus().code());
    ASSERT_FALSE(getCloner()->isActive());
}

}  // namespace repl
}  // namespace monger
