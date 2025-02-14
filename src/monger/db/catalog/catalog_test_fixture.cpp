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

#include "monger/db/catalog/catalog_test_fixture.h"

#include "monger/db/repl/replication_coordinator_mock.h"
#include "monger/db/repl/storage_interface_impl.h"
#include "monger/db/service_context_d_test_fixture.h"

namespace monger {

void CatalogTestFixture::setUp() {
    // Set up mongerd.
    ServiceContextMongerDTest::setUp();

    auto service = getServiceContext();

    // Set up ReplicationCoordinator and ensure that we are primary.
    auto replCoord = std::make_unique<repl::ReplicationCoordinatorMock>(service);
    ASSERT_OK(replCoord->setFollowerMode(repl::MemberState::RS_PRIMARY));
    repl::ReplicationCoordinator::set(service, std::move(replCoord));

    _storage = std::make_unique<repl::StorageInterfaceImpl>();
    _opCtx = cc().makeOperationContext();
}

void CatalogTestFixture::tearDown() {
    _storage.reset();
    _opCtx.reset();

    // Tear down mongerd.
    ServiceContextMongerDTest::tearDown();
}

OperationContext* CatalogTestFixture::operationContext() {
    return _opCtx.get();
}

repl::StorageInterface* CatalogTestFixture::storageInterface() {
    return _storage.get();
}

}  // namespace monger
