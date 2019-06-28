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

#include "monger/platform/basic.h"

#include "monger/db/service_context_d_test_fixture.h"

#include <memory>

#include "monger/base/checked_cast.h"
#include "monger/db/catalog/catalog_control.h"
#include "monger/db/catalog/collection_catalog.h"
#include "monger/db/catalog/collection_impl.h"
#include "monger/db/catalog/database_holder_impl.h"
#include "monger/db/concurrency/d_concurrency.h"
#include "monger/db/index/index_access_method_factory_impl.h"
#include "monger/db/index_builds_coordinator_mongerd.h"
#include "monger/db/logical_clock.h"
#include "monger/db/op_observer_registry.h"
#include "monger/db/service_entry_point_mongerd.h"
#include "monger/db/storage/storage_engine_init.h"
#include "monger/db/storage/storage_options.h"
#include "monger/util/assert_util.h"
#include "monger/util/periodic_runner_factory.h"

namespace monger {

ServiceContextMongerDTest::ServiceContextMongerDTest()
    : ServiceContextMongerDTest("ephemeralForTest") {}

ServiceContextMongerDTest::ServiceContextMongerDTest(std::string engine)
    : ServiceContextMongerDTest(engine, RepairAction::kNoRepair) {}

ServiceContextMongerDTest::ServiceContextMongerDTest(std::string engine, RepairAction repair)
    : _tempDir("service_context_d_test_fixture") {

    _stashedStorageParams.engine = std::exchange(storageGlobalParams.engine, std::move(engine));
    _stashedStorageParams.engineSetByUser =
        std::exchange(storageGlobalParams.engineSetByUser, true);
    _stashedStorageParams.repair =
        std::exchange(storageGlobalParams.repair, (repair == RepairAction::kRepair));

    auto const serviceContext = getServiceContext();
    serviceContext->setServiceEntryPoint(std::make_unique<ServiceEntryPointMongerd>(serviceContext));
    auto logicalClock = std::make_unique<LogicalClock>(serviceContext);
    LogicalClock::set(serviceContext, std::move(logicalClock));

    // Set up the periodic runner to allow background job execution for tests that require it.
    auto runner = makePeriodicRunner(getServiceContext());
    getServiceContext()->setPeriodicRunner(std::move(runner));

    storageGlobalParams.dbpath = _tempDir.path();

    initializeStorageEngine(serviceContext, StorageEngineInitFlags::kNone);

    DatabaseHolder::set(serviceContext, std::make_unique<DatabaseHolderImpl>());
    IndexAccessMethodFactory::set(serviceContext, std::make_unique<IndexAccessMethodFactoryImpl>());
    Collection::Factory::set(serviceContext, std::make_unique<CollectionImpl::FactoryImpl>());
    IndexBuildsCoordinator::set(serviceContext, std::make_unique<IndexBuildsCoordinatorMongerd>());
}

ServiceContextMongerDTest::~ServiceContextMongerDTest() {
    {
        auto opCtx = getClient()->makeOperationContext();
        Lock::GlobalLock glk(opCtx.get(), MODE_X);
        auto databaseHolder = DatabaseHolder::get(opCtx.get());
        databaseHolder->closeAll(opCtx.get());
    }

    IndexBuildsCoordinator::get(getServiceContext())->shutdown();

    shutdownGlobalStorageEngineCleanly(getServiceContext());

    std::swap(storageGlobalParams.engine, _stashedStorageParams.engine);
    std::swap(storageGlobalParams.engineSetByUser, _stashedStorageParams.engineSetByUser);
    std::swap(storageGlobalParams.repair, _stashedStorageParams.repair);
}

}  // namespace monger
