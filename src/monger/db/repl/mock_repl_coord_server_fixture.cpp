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

#include "monger/db/client.h"
#include "monger/db/curop.h"
#include "monger/db/db_raii.h"
#include "monger/db/dbdirectclient.h"
#include "monger/db/operation_context.h"
#include "monger/db/repl/drop_pending_collection_reaper.h"
#include "monger/db/repl/mock_repl_coord_server_fixture.h"
#include "monger/db/repl/oplog.h"
#include "monger/db/repl/oplog_entry.h"
#include "monger/db/repl/replication_consistency_markers_mock.h"
#include "monger/db/repl/replication_coordinator_mock.h"
#include "monger/db/repl/replication_process.h"
#include "monger/db/repl/replication_recovery_mock.h"
#include "monger/db/repl/storage_interface_mock.h"
#include "monger/db/service_context.h"
#include "monger/db/service_context_d_test_fixture.h"

namespace monger {

void MockReplCoordServerFixture::setUp() {
    ServiceContextMongerDTest::setUp();

    _opCtx = cc().makeOperationContext();

    auto service = getServiceContext();

    _storageInterface = new repl::StorageInterfaceMock();
    repl::StorageInterface::set(service,
                                std::unique_ptr<repl::StorageInterface>(_storageInterface));
    ASSERT_TRUE(_storageInterface == repl::StorageInterface::get(service));

    repl::ReplicationProcess::set(service,
                                  std::make_unique<repl::ReplicationProcess>(
                                      _storageInterface,
                                      std::make_unique<repl::ReplicationConsistencyMarkersMock>(),
                                      std::make_unique<repl::ReplicationRecoveryMock>()));

    ASSERT_OK(repl::ReplicationProcess::get(service)->initializeRollbackID(opCtx()));

    // Insert code path assumes existence of repl coordinator!
    repl::ReplSettings replSettings;
    replSettings.setReplSetString(
        ConnectionString::forReplicaSet("sessionTxnStateTest", {HostAndPort("a:1")}).toString());

    repl::ReplicationCoordinator::set(
        service, std::make_unique<repl::ReplicationCoordinatorMock>(service, replSettings));
    ASSERT_OK(
        repl::ReplicationCoordinator::get(service)->setFollowerMode(repl::MemberState::RS_PRIMARY));

    // Note: internal code does not allow implicit creation of non-capped oplog collection.
    DBDirectClient client(opCtx());
    ASSERT_TRUE(
        client.createCollection(NamespaceString::kRsOplogNamespace.ns(), 1024 * 1024, true));

    repl::setOplogCollectionName(service);
    repl::acquireOplogCollectionForLogging(opCtx());

    repl::DropPendingCollectionReaper::set(
        service,
        std::make_unique<repl::DropPendingCollectionReaper>(repl::StorageInterface::get(service)));
}

void MockReplCoordServerFixture::insertOplogEntry(const repl::OplogEntry& entry) {
    AutoGetCollection autoColl(opCtx(), NamespaceString::kRsOplogNamespace, MODE_IX);
    auto coll = autoColl.getCollection();
    ASSERT_TRUE(coll != nullptr);

    auto status = coll->insertDocument(opCtx(),
                                       InsertStatement(entry.toBSON()),
                                       &CurOp::get(opCtx())->debug(),
                                       /* fromMigrate */ false);
    ASSERT_OK(status);
}

OperationContext* MockReplCoordServerFixture::opCtx() {
    return _opCtx.get();
}

}  // namespace monger
