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

#include "monger/db/storage/sorted_data_interface_test_harness.h"

#include <memory>

#include "monger/db/storage/sorted_data_interface.h"
#include "monger/unittest/unittest.h"

namespace monger {
namespace {

// Add a key using a bulk builder.
TEST(SortedDataInterface, BuilderAddKey) {
    const auto harnessHelper(newSortedDataInterfaceHarnessHelper());
    const std::unique_ptr<SortedDataInterface> sorted(
        harnessHelper->newSortedDataInterface(/*unique=*/false, /*partial=*/false));

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT(sorted->isEmpty(opCtx.get()));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        const std::unique_ptr<SortedDataBuilderInterface> builder(
            sorted->getBulkBuilder(opCtx.get(), true));

        ASSERT_OK(builder->addKey(key1, loc1));
        ASSERT_EQUALS(SpecialFormatInserted::NoSpecialFormatInserted, builder->commit(false));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT_EQUALS(1, sorted->numEntries(opCtx.get()));
    }
}

// Add a reserved RecordId using a bulk builder.
TEST(SortedDataInterface, BuilderAddKeyWithReservedRecordId) {
    const auto harnessHelper(newSortedDataInterfaceHarnessHelper());
    const std::unique_ptr<SortedDataInterface> sorted(
        harnessHelper->newSortedDataInterface(/*unique=*/false, /*partial=*/false));
    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT(sorted->isEmpty(opCtx.get()));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        const std::unique_ptr<SortedDataBuilderInterface> builder(
            sorted->getBulkBuilder(opCtx.get(), true));

        RecordId reservedLoc(RecordId::ReservedId::kWildcardMultikeyMetadataId);
        ASSERT(reservedLoc.isReserved());

        ASSERT_OK(builder->addKey(key1, reservedLoc));
        ASSERT_EQUALS(SpecialFormatInserted::NoSpecialFormatInserted, builder->commit(false));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT_EQUALS(1, sorted->numEntries(opCtx.get()));
    }
}

// Add a compound key using a bulk builder.
TEST(SortedDataInterface, BuilderAddCompoundKey) {
    const auto harnessHelper(newSortedDataInterfaceHarnessHelper());
    const std::unique_ptr<SortedDataInterface> sorted(
        harnessHelper->newSortedDataInterface(/*unique=*/false, /*partial=*/false));

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT(sorted->isEmpty(opCtx.get()));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        const std::unique_ptr<SortedDataBuilderInterface> builder(
            sorted->getBulkBuilder(opCtx.get(), true));

        ASSERT_OK(builder->addKey(compoundKey1a, loc1));
        ASSERT_EQUALS(SpecialFormatInserted::NoSpecialFormatInserted, builder->commit(false));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT_EQUALS(1, sorted->numEntries(opCtx.get()));
    }
}

// Add the same key multiple times using a bulk builder and verify that
// the returned status is ErrorCodes::DuplicateKey when duplicates are
// not allowed.
TEST(SortedDataInterface, BuilderAddSameKey) {
    const auto harnessHelper(newSortedDataInterfaceHarnessHelper());
    const std::unique_ptr<SortedDataInterface> sorted(
        harnessHelper->newSortedDataInterface(/*unique=*/true, /*partial=*/false));

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT(sorted->isEmpty(opCtx.get()));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        const std::unique_ptr<SortedDataBuilderInterface> builder(
            sorted->getBulkBuilder(opCtx.get(), false));

        ASSERT_OK(builder->addKey(key1, loc1));
        ASSERT_EQUALS(ErrorCodes::DuplicateKey, builder->addKey(key1, loc2));
        ASSERT_EQUALS(SpecialFormatInserted::NoSpecialFormatInserted, builder->commit(false));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT_EQUALS(1, sorted->numEntries(opCtx.get()));
    }
}

// Add the same key multiple times using a bulk builder and verify that
// the returned status is OK when duplicates are allowed.
TEST(SortedDataInterface, BuilderAddSameKeyWithDupsAllowed) {
    const auto harnessHelper(newSortedDataInterfaceHarnessHelper());
    const std::unique_ptr<SortedDataInterface> sorted(
        harnessHelper->newSortedDataInterface(/*unique=*/false, /*partial=*/false));

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT(sorted->isEmpty(opCtx.get()));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        const std::unique_ptr<SortedDataBuilderInterface> builder(
            sorted->getBulkBuilder(opCtx.get(), true /* allow duplicates */));

        ASSERT_OK(builder->addKey(key1, loc1));
        ASSERT_OK(builder->addKey(key1, loc2));
        ASSERT_EQUALS(SpecialFormatInserted::NoSpecialFormatInserted, builder->commit(false));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT_EQUALS(2, sorted->numEntries(opCtx.get()));
    }
}

// Add multiple keys using a bulk builder.
TEST(SortedDataInterface, BuilderAddMultipleKeys) {
    const auto harnessHelper(newSortedDataInterfaceHarnessHelper());
    const std::unique_ptr<SortedDataInterface> sorted(
        harnessHelper->newSortedDataInterface(/*unique=*/false, /*partial=*/false));

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT(sorted->isEmpty(opCtx.get()));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        const std::unique_ptr<SortedDataBuilderInterface> builder(
            sorted->getBulkBuilder(opCtx.get(), true));

        ASSERT_OK(builder->addKey(key1, loc1));
        ASSERT_OK(builder->addKey(key2, loc2));
        ASSERT_OK(builder->addKey(key3, loc3));
        ASSERT_EQUALS(SpecialFormatInserted::NoSpecialFormatInserted, builder->commit(false));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT_EQUALS(3, sorted->numEntries(opCtx.get()));
    }
}

// Add multiple compound keys using a bulk builder.
TEST(SortedDataInterface, BuilderAddMultipleCompoundKeys) {
    const auto harnessHelper(newSortedDataInterfaceHarnessHelper());
    const std::unique_ptr<SortedDataInterface> sorted(
        harnessHelper->newSortedDataInterface(/*unique=*/false, /*partial=*/false));

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT(sorted->isEmpty(opCtx.get()));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        const std::unique_ptr<SortedDataBuilderInterface> builder(
            sorted->getBulkBuilder(opCtx.get(), true));

        ASSERT_OK(builder->addKey(compoundKey1a, loc1));
        ASSERT_OK(builder->addKey(compoundKey1b, loc2));
        ASSERT_OK(builder->addKey(compoundKey1c, loc4));
        ASSERT_OK(builder->addKey(compoundKey2b, loc3));
        ASSERT_OK(builder->addKey(compoundKey3a, loc5));
        ASSERT_EQUALS(SpecialFormatInserted::NoSpecialFormatInserted, builder->commit(false));
    }

    {
        const ServiceContext::UniqueOperationContext opCtx(harnessHelper->newOperationContext());
        ASSERT_EQUALS(5, sorted->numEntries(opCtx.get()));
    }
}

}  // namespace
}  // namespace monger
