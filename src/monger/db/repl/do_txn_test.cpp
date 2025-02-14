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

#include "monger/db/catalog/collection_options.h"
#include "monger/db/catalog/database_holder.h"
#include "monger/db/client.h"
#include "monger/db/op_observer_noop.h"
#include "monger/db/op_observer_registry.h"
#include "monger/db/repl/do_txn.h"
#include "monger/db/repl/oplog_interface_local.h"
#include "monger/db/repl/repl_client_info.h"
#include "monger/db/repl/replication_coordinator_mock.h"
#include "monger/db/repl/storage_interface_impl.h"
#include "monger/db/repl/storage_interface_mock.h"
#include "monger/db/s/op_observer_sharding_impl.h"
#include "monger/db/service_context_d_test_fixture.h"
#include "monger/db/session_catalog_mongerd.h"
#include "monger/db/transaction_participant.h"
#include "monger/logger/logger.h"
#include "monger/rpc/get_status_from_command_result.h"

namespace monger {
namespace repl {
namespace {

boost::optional<OplogEntry> onAllTransactionCommit(OperationContext* opCtx) {
    OplogInterfaceLocal oplogInterface(opCtx);
    auto oplogIter = oplogInterface.makeIterator();
    auto opEntry = unittest::assertGet(oplogIter->next());
    return unittest::assertGet(OplogEntry::parse(opEntry.first));
}

/**
 * Mock OpObserver that tracks doTxn commit events.
 */
class OpObserverMock : public OpObserverNoop {
public:
    /**
     * Called by doTxn() when ops are ready to commit.
     */
    void onUnpreparedTransactionCommit(OperationContext* opCtx,
                                       const std::vector<repl::ReplOperation>& statements) override;


    /**
     * Called by doTxn() when ops are ready to commit.
     */
    void onPreparedTransactionCommit(
        OperationContext* opCtx,
        OplogSlot commitOplogEntryOpTime,
        Timestamp commitTimestamp,
        const std::vector<repl::ReplOperation>& statements) noexcept override;

    // If present, holds the applyOps oplog entry written out by the ObObserverImpl
    // onPreparedTransactionCommit or onUnpreparedTransactionCommit.
    boost::optional<OplogEntry> applyOpsOplogEntry;
};

void OpObserverMock::onUnpreparedTransactionCommit(
    OperationContext* opCtx, const std::vector<repl::ReplOperation>& statements) {

    applyOpsOplogEntry = onAllTransactionCommit(opCtx);
}

void OpObserverMock::onPreparedTransactionCommit(
    OperationContext* opCtx,
    OplogSlot commitOplogEntryOpTime,
    Timestamp commitTimestamp,
    const std::vector<repl::ReplOperation>& statements) noexcept {
    applyOpsOplogEntry = onAllTransactionCommit(opCtx);
}

/**
 * Test fixture for doTxn().
 */
class DoTxnTest : public ServiceContextMongerDTest {
private:
    void setUp() override;
    void tearDown() override;

protected:
    OperationContext* opCtx() {
        return _opCtx.get();
    }

    void checkTxnTable() {
        auto result = _storage->findById(
            opCtx(),
            NamespaceString::kSessionTransactionsTableNamespace,
            BSON(SessionTxnRecord::kSessionIdFieldName << opCtx()->getLogicalSessionId()->toBSON())
                .firstElement());
        if (!_opObserver->applyOpsOplogEntry) {
            ASSERT_NOT_OK(result);
            return;
        }
        auto txnRecord = SessionTxnRecord::parse(IDLParserErrorContext("parse txn record for test"),
                                                 unittest::assertGet(result));

        ASSERT(opCtx()->getTxnNumber());
        ASSERT_EQ(*opCtx()->getTxnNumber(), txnRecord.getTxnNum());
        ASSERT_EQ(_opObserver->applyOpsOplogEntry->getOpTime(), txnRecord.getLastWriteOpTime());
        ASSERT(_opObserver->applyOpsOplogEntry->getWallClockTime());
        ASSERT_EQ(*_opObserver->applyOpsOplogEntry->getWallClockTime(),
                  txnRecord.getLastWriteDate());
    }

    OpObserverMock* _opObserver = nullptr;
    std::unique_ptr<StorageInterface> _storage;
    ServiceContext::UniqueOperationContext _opCtx;
    boost::optional<MongerDOperationContextSession> _ocs;
};

void DoTxnTest::setUp() {
    // Set up mongerd.
    ServiceContextMongerDTest::setUp();

    const auto service = getServiceContext();
    _opCtx = cc().makeOperationContext();

    // Set up ReplicationCoordinator and create oplog.
    ReplicationCoordinator::set(service, std::make_unique<ReplicationCoordinatorMock>(service));
    setOplogCollectionName(service);
    createOplog(_opCtx.get());

    // Ensure that we are primary.
    auto replCoord = ReplicationCoordinator::get(_opCtx.get());
    ASSERT_OK(replCoord->setFollowerMode(MemberState::RS_PRIMARY));

    // Set up session catalog
    MongerDSessionCatalog::onStepUp(_opCtx.get());

    // Need the OpObserverImpl in the registry in order for doTxn to work.
    OpObserverRegistry* opObserverRegistry =
        dynamic_cast<OpObserverRegistry*>(service->getOpObserver());
    opObserverRegistry->addObserver(std::make_unique<OpObserverShardingImpl>());

    // Use OpObserverMock to track applyOps calls generated by doTxn().
    auto opObserver = std::make_unique<OpObserverMock>();
    _opObserver = opObserver.get();
    opObserverRegistry->addObserver(std::move(opObserver));

    // This test uses StorageInterface to create collections and inspect documents inside
    // collections.
    _storage = std::make_unique<StorageInterfaceImpl>();

    // We also need to give replication a StorageInterface for checking out the transaction.
    // The test storage engine doesn't support the necessary call (getPointInTimeReadTimestamp()),
    // so we use a mock.
    repl::StorageInterface::set(service, std::make_unique<StorageInterfaceMock>());

    // Set up the transaction and session.
    _opCtx->setLogicalSessionId(makeLogicalSessionIdForTest());
    _opCtx->setTxnNumber(0);  // TxnNumber can always be 0 because we have a new session.
    _ocs.emplace(_opCtx.get());

    auto txnParticipant = TransactionParticipant::get(opCtx());
    txnParticipant.beginOrContinue(opCtx(), *opCtx()->getTxnNumber(), false, true);
    txnParticipant.unstashTransactionResources(opCtx(), "doTxn");
}

void DoTxnTest::tearDown() {
    _ocs = boost::none;
    _opCtx = nullptr;
    _storage = {};
    _opObserver = nullptr;

    // Reset default log level in case it was changed.
    logger::globalLogDomain()->setMinimumLoggedSeverity(logger::LogComponent::kReplication,
                                                        logger::LogSeverity::Debug(0));

    ServiceContextMongerDTest::tearDown();
}

/**
 * Fixes up result document returned by doTxn and converts to Status.
 */
Status getStatusFromDoTxnResult(const BSONObj& result) {
    if (result["ok"]) {
        return getStatusFromCommandResult(result);
    }

    BSONObjBuilder builder;
    builder.appendElements(result);
    auto code = result.getIntField("code");
    builder.appendIntOrLL("ok", code == 0);
    auto newResult = builder.obj();
    return getStatusFromCommandResult(newResult);
}

BSONObj makeInsertOperation(const NamespaceString& nss,
                            const OptionalCollectionUUID& uuid,
                            const BSONObj& documentToInsert) {
    return uuid ? BSON("op"
                       << "i"
                       << "ns"
                       << nss.ns()
                       << "o"
                       << documentToInsert
                       << "ui"
                       << *uuid)
                : BSON("op"
                       << "i"
                       << "ns"
                       << nss.ns()
                       << "o"
                       << documentToInsert);
}

/**
 * Creates an doTxn command object with a single insert operation.
 */
BSONObj makeDoTxnWithInsertOperation(const NamespaceString& nss,
                                     const OptionalCollectionUUID& uuid,
                                     const BSONObj& documentToInsert) {
    auto insertOp = makeInsertOperation(nss, uuid, documentToInsert);
    return BSON("doTxn" << BSON_ARRAY(insertOp));
}

/**
 * Creates an applyOps command object with a single insert operation.
 */
BSONObj makeApplyOpsWithInsertOperation(const NamespaceString& nss,
                                        const OptionalCollectionUUID& uuid,
                                        const BSONObj& documentToInsert) {
    auto insertOp = makeInsertOperation(nss, uuid, documentToInsert);
    return BSON("applyOps" << BSON_ARRAY(insertOp));
}

TEST_F(DoTxnTest, AtomicDoTxnInsertIntoNonexistentCollectionReturnsNamespaceNotFoundInResult) {
    NamespaceString nss("test.t");
    auto documentToInsert = BSON("_id" << 0);
    auto cmdObj = makeDoTxnWithInsertOperation(nss, boost::none, documentToInsert);
    BSONObjBuilder resultBuilder;
    ASSERT_EQUALS(ErrorCodes::UnknownError, doTxn(opCtx(), "test", cmdObj, &resultBuilder));
    auto result = resultBuilder.obj();
    auto status = getStatusFromDoTxnResult(result);
    ASSERT_EQUALS(ErrorCodes::NamespaceNotFound, status);
    checkTxnTable();
}

TEST_F(DoTxnTest, AtomicDoTxnInsertWithUuidIntoCollectionWithUuid) {
    NamespaceString nss("test.t");

    auto uuid = UUID::gen();

    CollectionOptions collectionOptions;
    collectionOptions.uuid = uuid;
    ASSERT_OK(_storage->createCollection(opCtx(), nss, collectionOptions));

    auto documentToInsert = BSON("_id" << 0);
    auto cmdObj = makeDoTxnWithInsertOperation(nss, uuid, documentToInsert);
    auto expectedCmdObj = makeApplyOpsWithInsertOperation(nss, uuid, documentToInsert);
    BSONObjBuilder resultBuilder;
    ASSERT_OK(doTxn(opCtx(), "test", cmdObj, &resultBuilder));
    ASSERT_EQ(expectedCmdObj.woCompare(_opObserver->applyOpsOplogEntry->getObject(),
                                       BSONObj(),
                                       BSONObj::ComparisonRules::kIgnoreFieldOrder |
                                           BSONObj::ComparisonRules::kConsiderFieldName),
              0)
        << "expected: " << expectedCmdObj
        << " got: " << _opObserver->applyOpsOplogEntry->getObject();
    checkTxnTable();
}

TEST_F(DoTxnTest, AtomicDoTxnInsertWithUuidIntoCollectionWithOtherUuid) {
    NamespaceString nss("test.t");

    auto doTxnUuid = UUID::gen();

    // Collection has a different UUID.
    CollectionOptions collectionOptions;
    collectionOptions.uuid = UUID::gen();
    ASSERT_NOT_EQUALS(doTxnUuid, *collectionOptions.uuid);
    ASSERT_OK(_storage->createCollection(opCtx(), nss, collectionOptions));

    // The doTxn returns a NamespaceNotFound error because of the failed UUID lookup
    // even though a collection exists with the same namespace as the insert operation.
    auto documentToInsert = BSON("_id" << 0);
    auto cmdObj = makeDoTxnWithInsertOperation(nss, doTxnUuid, documentToInsert);
    BSONObjBuilder resultBuilder;
    ASSERT_EQUALS(ErrorCodes::UnknownError, doTxn(opCtx(), "test", cmdObj, &resultBuilder));
    auto result = resultBuilder.obj();
    auto status = getStatusFromDoTxnResult(result);
    ASSERT_EQUALS(ErrorCodes::NamespaceNotFound, status);
    checkTxnTable();
}

TEST_F(DoTxnTest, AtomicDoTxnInsertWithoutUuidIntoCollectionWithUuid) {
    NamespaceString nss("test.t");

    auto uuid = UUID::gen();

    CollectionOptions collectionOptions;
    collectionOptions.uuid = uuid;
    ASSERT_OK(_storage->createCollection(opCtx(), nss, collectionOptions));

    auto documentToInsert = BSON("_id" << 0);
    auto cmdObj = makeDoTxnWithInsertOperation(nss, boost::none, documentToInsert);
    BSONObjBuilder resultBuilder;
    ASSERT_OK(doTxn(opCtx(), "test", cmdObj, &resultBuilder));

    // Insert operation provided by caller did not contain collection uuid but doTxn() should add
    // the uuid to the oplog entry.
    auto expectedCmdObj = makeApplyOpsWithInsertOperation(nss, uuid, documentToInsert);
    ASSERT_EQ(expectedCmdObj.woCompare(_opObserver->applyOpsOplogEntry->getObject(),
                                       BSONObj(),
                                       BSONObj::ComparisonRules::kIgnoreFieldOrder |
                                           BSONObj::ComparisonRules::kConsiderFieldName),
              0)
        << "expected: " << expectedCmdObj
        << " got: " << _opObserver->applyOpsOplogEntry->getObject();
    checkTxnTable();
}

}  // namespace
}  // namespace repl
}  // namespace monger
