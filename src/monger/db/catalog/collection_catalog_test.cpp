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
#include "monger/db/catalog/collection_catalog.h"

#include <algorithm>
#include <boost/optional/optional_io.hpp>

#include "monger/db/catalog/catalog_test_fixture.h"
#include "monger/db/catalog/collection_catalog_entry_mock.h"
#include "monger/db/catalog/collection_catalog_helper.h"
#include "monger/db/catalog/collection_mock.h"
#include "monger/db/concurrency/lock_manager_defs.h"
#include "monger/db/operation_context_noop.h"
#include "monger/db/storage/durable_catalog.h"
#include "monger/unittest/death_test.h"
#include "monger/unittest/unittest.h"

using namespace monger;

/**
 * A test fixture that creates a CollectionCatalog and Collection* pointer to store in it.
 */
class CollectionCatalogTest : public unittest::Test {
public:
    CollectionCatalogTest()
        : nss("testdb", "testcol"),
          col(nullptr),
          colUUID(CollectionUUID::gen()),
          nextUUID(CollectionUUID::gen()),
          prevUUID(CollectionUUID::gen()) {
        if (prevUUID > colUUID)
            std::swap(prevUUID, colUUID);
        if (colUUID > nextUUID)
            std::swap(colUUID, nextUUID);
        if (prevUUID > colUUID)
            std::swap(prevUUID, colUUID);
        ASSERT_GT(colUUID, prevUUID);
        ASSERT_GT(nextUUID, colUUID);

        auto collection = std::make_unique<CollectionMock>(nss);
        auto catalogEntry = std::make_unique<CollectionCatalogEntryMock>(nss.ns());
        col = collection.get();
        // Register dummy collection in catalog.
        catalog.registerCollection(colUUID, std::move(catalogEntry), std::move(collection));
    }

protected:
    CollectionCatalog catalog;
    OperationContextNoop opCtx;
    NamespaceString nss;
    CollectionMock* col;
    CollectionUUID colUUID;
    CollectionUUID nextUUID;
    CollectionUUID prevUUID;
};

class CollectionCatalogIterationTest : public unittest::Test {
public:
    void setUp() {
        for (int counter = 0; counter < 5; ++counter) {
            NamespaceString fooNss("foo", "coll" + std::to_string(counter));
            NamespaceString barNss("bar", "coll" + std::to_string(counter));

            auto fooUuid = CollectionUUID::gen();
            auto fooColl = std::make_unique<CollectionMock>(fooNss);
            auto fooCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(fooNss.ns());

            auto barUuid = CollectionUUID::gen();
            auto barColl = std::make_unique<CollectionMock>(barNss);
            auto barCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(barNss.ns());

            dbMap["foo"].insert(std::make_pair(fooUuid, fooColl.get()));
            dbMap["bar"].insert(std::make_pair(barUuid, barColl.get()));
            catalog.registerCollection(fooUuid, std::move(fooCatalogEntry), std::move(fooColl));
            catalog.registerCollection(barUuid, std::move(barCatalogEntry), std::move(barColl));
        }
    }

    void tearDown() {
        for (auto& it : dbMap) {
            for (auto& kv : it.second) {
                catalog.deregisterCollection(kv.first);
            }
        }
    }

    std::map<CollectionUUID, CollectionMock*>::iterator collsIterator(std::string dbName) {
        auto it = dbMap.find(dbName);
        ASSERT(it != dbMap.end());
        return it->second.begin();
    }

    std::map<CollectionUUID, CollectionMock*>::iterator collsIteratorEnd(std::string dbName) {
        auto it = dbMap.find(dbName);
        ASSERT(it != dbMap.end());
        return it->second.end();
    }

    void checkCollections(std::string dbName) {
        unsigned long counter = 0;

        for (auto[orderedIt, catalogIt] = std::tuple{collsIterator(dbName), catalog.begin(dbName)};
             catalogIt != catalog.end() && orderedIt != collsIteratorEnd(dbName);
             ++catalogIt, ++orderedIt) {

            auto catalogColl = *catalogIt;
            ASSERT(catalogColl != nullptr);
            auto orderedColl = orderedIt->second;
            ASSERT_EQ(catalogColl->ns(), orderedColl->ns());
            ++counter;
        }

        ASSERT_EQUALS(counter, dbMap[dbName].size());
    }

    void dropColl(const std::string dbName, CollectionUUID uuid) {
        dbMap[dbName].erase(uuid);
    }

protected:
    CollectionCatalog catalog;
    OperationContextNoop opCtx;
    std::map<std::string, std::map<CollectionUUID, CollectionMock*>> dbMap;
};

class CollectionCatalogResourceMapTest : public unittest::Test {
public:
    void setUp() {
        // The first and second collection namespaces map to the same ResourceId.
        firstCollection = "1661880728";
        secondCollection = "1626936312";

        firstResourceId = ResourceId(RESOURCE_COLLECTION, firstCollection);
        secondResourceId = ResourceId(RESOURCE_COLLECTION, secondCollection);
        ASSERT_EQ(firstResourceId, secondResourceId);

        thirdCollection = "2930102946";
        thirdResourceId = ResourceId(RESOURCE_COLLECTION, thirdCollection);
        ASSERT_NE(firstResourceId, thirdResourceId);
    }

protected:
    std::string firstCollection;
    ResourceId firstResourceId;

    std::string secondCollection;
    ResourceId secondResourceId;

    std::string thirdCollection;
    ResourceId thirdResourceId;

    CollectionCatalog catalog;
};

TEST_F(CollectionCatalogResourceMapTest, EmptyTest) {
    boost::optional<std::string> resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(boost::none, resource);

    catalog.removeResource(secondResourceId, secondCollection);
    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(boost::none, resource);
}

TEST_F(CollectionCatalogResourceMapTest, InsertTest) {
    catalog.addResource(firstResourceId, firstCollection);
    boost::optional<std::string> resource = catalog.lookupResourceName(thirdResourceId);
    ASSERT_EQ(boost::none, resource);

    catalog.addResource(thirdResourceId, thirdCollection);

    resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(firstCollection, *resource);

    resource = catalog.lookupResourceName(thirdResourceId);
    ASSERT_EQ(thirdCollection, resource);
}

TEST_F(CollectionCatalogResourceMapTest, RemoveTest) {
    catalog.addResource(firstResourceId, firstCollection);
    catalog.addResource(thirdResourceId, thirdCollection);

    // This fails to remove the resource because of an invalid namespace.
    catalog.removeResource(firstResourceId, "BadNamespace");
    boost::optional<std::string> resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(firstCollection, *resource);

    catalog.removeResource(firstResourceId, firstCollection);
    catalog.removeResource(firstResourceId, firstCollection);
    catalog.removeResource(thirdResourceId, thirdCollection);

    resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(boost::none, resource);

    resource = catalog.lookupResourceName(thirdResourceId);
    ASSERT_EQ(boost::none, resource);
}

TEST_F(CollectionCatalogResourceMapTest, CollisionTest) {
    // firstCollection and secondCollection map to the same ResourceId.
    catalog.addResource(firstResourceId, firstCollection);
    catalog.addResource(secondResourceId, secondCollection);

    // Looking up the namespace on a ResourceId while it has a collision should
    // return the empty string.
    boost::optional<std::string> resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(boost::none, resource);

    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(boost::none, resource);

    // We remove a namespace, resolving the collision.
    catalog.removeResource(firstResourceId, firstCollection);
    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(secondCollection, *resource);

    // Adding the same namespace twice does not create a collision.
    catalog.addResource(secondResourceId, secondCollection);
    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(secondCollection, *resource);

    // The map should function normally for entries without collisions.
    catalog.addResource(firstResourceId, firstCollection);
    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(boost::none, resource);

    catalog.addResource(thirdResourceId, thirdCollection);
    resource = catalog.lookupResourceName(thirdResourceId);
    ASSERT_EQ(thirdCollection, *resource);

    catalog.removeResource(thirdResourceId, thirdCollection);
    resource = catalog.lookupResourceName(thirdResourceId);
    ASSERT_EQ(boost::none, resource);

    catalog.removeResource(firstResourceId, firstCollection);
    catalog.removeResource(secondResourceId, secondCollection);

    resource = catalog.lookupResourceName(firstResourceId);
    ASSERT_EQ(boost::none, resource);

    resource = catalog.lookupResourceName(secondResourceId);
    ASSERT_EQ(boost::none, resource);
}

class CollectionCatalogResourceTest : public unittest::Test {
public:
    void setUp() {
        for (int i = 0; i < 5; i++) {
            NamespaceString nss("resourceDb", "coll" + std::to_string(i));
            auto coll = std::make_unique<CollectionMock>(nss);
            auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(nss.ns());
            auto uuid = coll->uuid();

            catalog.registerCollection(uuid.get(), std::move(newCatalogEntry), std::move(coll));
        }

        int numEntries = 0;
        for (auto it = catalog.begin("resourceDb"); it != catalog.end(); it++) {
            auto coll = *it;
            std::string collName = coll->ns().ns();
            ResourceId rid(RESOURCE_COLLECTION, collName);

            ASSERT_NE(catalog.lookupResourceName(rid), boost::none);
            numEntries++;
        }
        ASSERT_EQ(5, numEntries);
    }

    void tearDown() {
        for (auto it = catalog.begin("resourceDb"); it != catalog.end(); ++it) {
            auto coll = *it;
            auto uuid = coll->uuid().get();
            if (!coll) {
                break;
            }

            catalog.deregisterCollection(uuid);
        }

        int numEntries = 0;
        for (auto it = catalog.begin("resourceDb"); it != catalog.end(); it++) {
            numEntries++;
        }
        ASSERT_EQ(0, numEntries);
    }

protected:
    OperationContextNoop opCtx;
    CollectionCatalog catalog;
};

namespace {

TEST_F(CollectionCatalogResourceTest, RemoveAllResources) {
    catalog.deregisterAllCatalogEntriesAndCollectionObjects();

    const std::string dbName = "resourceDb";
    auto rid = ResourceId(RESOURCE_DATABASE, dbName);
    ASSERT_EQ(boost::none, catalog.lookupResourceName(rid));

    for (int i = 0; i < 5; i++) {
        NamespaceString nss("resourceDb", "coll" + std::to_string(i));
        rid = ResourceId(RESOURCE_COLLECTION, nss.ns());
        ASSERT_EQ(boost::none, catalog.lookupResourceName((rid)));
    }
}

TEST_F(CollectionCatalogResourceTest, LookupDatabaseResource) {
    const std::string dbName = "resourceDb";
    auto rid = ResourceId(RESOURCE_DATABASE, dbName);
    boost::optional<std::string> ridStr = catalog.lookupResourceName(rid);

    ASSERT(ridStr);
    ASSERT(ridStr->find(dbName) != std::string::npos);
}

TEST_F(CollectionCatalogResourceTest, LookupMissingDatabaseResource) {
    const std::string dbName = "missingDb";
    auto rid = ResourceId(RESOURCE_DATABASE, dbName);
    ASSERT(!catalog.lookupResourceName(rid));
}

TEST_F(CollectionCatalogResourceTest, LookupCollectionResource) {
    const std::string collNs = "resourceDb.coll1";
    auto rid = ResourceId(RESOURCE_COLLECTION, collNs);
    boost::optional<std::string> ridStr = catalog.lookupResourceName(rid);

    ASSERT(ridStr);
    ASSERT(ridStr->find(collNs) != std::string::npos);
}

TEST_F(CollectionCatalogResourceTest, LookupMissingCollectionResource) {
    const std::string dbName = "resourceDb.coll5";
    auto rid = ResourceId(RESOURCE_COLLECTION, dbName);
    ASSERT(!catalog.lookupResourceName(rid));
}

TEST_F(CollectionCatalogResourceTest, RemoveCollection) {
    const std::string collNs = "resourceDb.coll1";
    auto coll = catalog.lookupCollectionByNamespace(NamespaceString(collNs));
    catalog.deregisterCollection(coll->uuid().get());
    auto rid = ResourceId(RESOURCE_COLLECTION, collNs);
    ASSERT(!catalog.lookupResourceName(rid));
}

// Create an iterator over the CollectionCatalog and assert that all collections are present.
// Iteration ends when the end of the catalog is reached.
TEST_F(CollectionCatalogIterationTest, EndAtEndOfCatalog) {
    checkCollections("foo");
}

// Create an iterator over the CollectionCatalog and test that all collections are present.
// Iteration ends
// when the end of a database-specific section of the catalog is reached.
TEST_F(CollectionCatalogIterationTest, EndAtEndOfSection) {
    checkCollections("bar");
}

// Delete an entry in the catalog while iterating.
TEST_F(CollectionCatalogIterationTest, InvalidateEntry) {
    auto it = catalog.begin("bar");

    // Invalidate bar.coll1.
    for (auto collsIt = collsIterator("bar"); collsIt != collsIteratorEnd("bar"); ++collsIt) {
        if (collsIt->second->ns().ns() == "bar.coll1") {
            catalog.deregisterCollection(collsIt->first);
            dropColl("bar", collsIt->first);
            break;
        }
    }


    for (; it != catalog.end(); ++it) {
        auto coll = *it;
        ASSERT(coll && coll->ns().ns() != "bar.coll1");
    }
}

// Delete the entry pointed to by the iterator and dereference the iterator.
TEST_F(CollectionCatalogIterationTest, InvalidateAndDereference) {
    auto it = catalog.begin("bar");
    auto collsIt = collsIterator("bar");
    auto uuid = collsIt->first;
    catalog.deregisterCollection(uuid);
    ++collsIt;

    ASSERT(it != catalog.end());
    auto catalogColl = *it;
    ASSERT(catalogColl != nullptr);
    ASSERT_EQUALS(catalogColl->ns(), collsIt->second->ns());

    dropColl("bar", uuid);
}

// Delete the last entry for a database while pointing to it and dereference the iterator.
TEST_F(CollectionCatalogIterationTest, InvalidateLastEntryAndDereference) {
    auto it = catalog.begin("bar");
    NamespaceString lastNs;
    boost::optional<CollectionUUID> uuid;
    for (auto collsIt = collsIterator("bar"); collsIt != collsIteratorEnd("bar"); ++collsIt) {
        lastNs = collsIt->second->ns();
        uuid = collsIt->first;
    }

    // Increment until it points to the last collection.
    for (; it != catalog.end(); ++it) {
        auto coll = *it;
        ASSERT(coll != nullptr);
        if (coll->ns() == lastNs) {
            break;
        }
    }

    catalog.deregisterCollection(*uuid);
    dropColl("bar", *uuid);
    ASSERT(*it == nullptr);
}

// Delete the last entry in the map while pointing to it and dereference the iterator.
TEST_F(CollectionCatalogIterationTest, InvalidateLastEntryInMapAndDereference) {
    auto it = catalog.begin("foo");
    NamespaceString lastNs;
    boost::optional<CollectionUUID> uuid;
    for (auto collsIt = collsIterator("foo"); collsIt != collsIteratorEnd("foo"); ++collsIt) {
        lastNs = collsIt->second->ns();
        uuid = collsIt->first;
    }

    // Increment until it points to the last collection.
    for (; it != catalog.end(); ++it) {
        auto coll = *it;
        ASSERT(coll != nullptr);
        if (coll->ns() == lastNs) {
            break;
        }
    }

    catalog.deregisterCollection(*uuid);
    dropColl("foo", *uuid);
    ASSERT(*it == nullptr);
}

TEST_F(CollectionCatalogIterationTest, GetUUIDWontRepositionEvenIfEntryIsDropped) {
    auto it = catalog.begin("bar");
    auto collsIt = collsIterator("bar");
    auto uuid = collsIt->first;
    catalog.deregisterCollection(uuid);
    dropColl("bar", uuid);

    ASSERT_EQUALS(uuid, it.uuid());
}

TEST_F(CollectionCatalogTest, OnCreateCollection) {
    ASSERT(catalog.lookupCollectionByUUID(colUUID) == col);
}

TEST_F(CollectionCatalogTest, LookupCollectionByUUID) {
    // Ensure the string value of the NamespaceString of the obtained Collection is equal to
    // nss.ns().
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(colUUID)->ns().ns(), nss.ns());
    // Ensure lookups of unknown UUIDs result in null pointers.
    ASSERT(catalog.lookupCollectionByUUID(CollectionUUID::gen()) == nullptr);
}

TEST_F(CollectionCatalogTest, LookupNSSByUUID) {
    // Ensure the string value of the obtained NamespaceString is equal to nss.ns().
    ASSERT_EQUALS(catalog.lookupNSSByUUID(colUUID)->ns(), nss.ns());
    // Ensure namespace lookups of unknown UUIDs result in empty NamespaceStrings.
    ASSERT_EQUALS(catalog.lookupNSSByUUID(CollectionUUID::gen()), boost::none);
}

TEST_F(CollectionCatalogTest, InsertAfterLookup) {
    auto newUUID = CollectionUUID::gen();
    NamespaceString newNss(nss.db(), "newcol");
    auto newCollUnique = std::make_unique<CollectionMock>(newNss);
    auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(newNss.ns());
    auto newCol = newCollUnique.get();

    // Ensure that looking up non-existing UUIDs doesn't affect later registration of those UUIDs.
    ASSERT(catalog.lookupCollectionByUUID(newUUID) == nullptr);
    ASSERT_EQUALS(catalog.lookupNSSByUUID(newUUID), boost::none);
    catalog.registerCollection(newUUID, std::move(newCatalogEntry), std::move(newCollUnique));
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(newUUID), newCol);
    ASSERT_EQUALS(*catalog.lookupNSSByUUID(colUUID), nss);
}

TEST_F(CollectionCatalogTest, OnDropCollection) {
    catalog.deregisterCollection(colUUID);
    // Ensure the lookup returns a null pointer upon removing the colUUID entry.
    ASSERT(catalog.lookupCollectionByUUID(colUUID) == nullptr);
}

TEST_F(CollectionCatalogTest, RenameCollection) {
    auto uuid = CollectionUUID::gen();
    NamespaceString oldNss(nss.db(), "oldcol");
    auto collUnique = std::make_unique<CollectionMock>(oldNss);
    auto catalogEntry = std::make_unique<CollectionCatalogEntryMock>(oldNss.ns());
    auto collection = collUnique.get();
    catalog.registerCollection(uuid, std::move(catalogEntry), std::move(collUnique));
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(uuid), collection);

    NamespaceString newNss(nss.db(), "newcol");
    catalog.setCollectionNamespace(&opCtx, collection, oldNss, newNss);
    ASSERT_EQ(collection->ns(), newNss);
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(uuid), collection);
}

TEST_F(CollectionCatalogTest, LookupNSSByUUIDForClosedCatalogReturnsOldNSSIfDropped) {
    catalog.onCloseCatalog(&opCtx);
    catalog.deregisterCollection(colUUID);
    ASSERT(catalog.lookupCollectionByUUID(colUUID) == nullptr);
    ASSERT_EQUALS(*catalog.lookupNSSByUUID(colUUID), nss);
    catalog.onOpenCatalog(&opCtx);
    ASSERT_EQUALS(catalog.lookupNSSByUUID(colUUID), boost::none);
}

TEST_F(CollectionCatalogTest, LookupNSSByUUIDForClosedCatalogReturnsNewlyCreatedNSS) {
    auto newUUID = CollectionUUID::gen();
    NamespaceString newNss(nss.db(), "newcol");
    auto newCollUnique = std::make_unique<CollectionMock>(newNss);
    auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(newNss.ns());
    auto newCol = newCollUnique.get();

    // Ensure that looking up non-existing UUIDs doesn't affect later registration of those UUIDs.
    catalog.onCloseCatalog(&opCtx);
    ASSERT(catalog.lookupCollectionByUUID(newUUID) == nullptr);
    ASSERT_EQUALS(catalog.lookupNSSByUUID(newUUID), boost::none);
    catalog.registerCollection(newUUID, std::move(newCatalogEntry), std::move(newCollUnique));
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(newUUID), newCol);
    ASSERT_EQUALS(*catalog.lookupNSSByUUID(colUUID), nss);

    // Ensure that collection still exists after opening the catalog again.
    catalog.onOpenCatalog(&opCtx);
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(newUUID), newCol);
    ASSERT_EQUALS(*catalog.lookupNSSByUUID(colUUID), nss);
}

TEST_F(CollectionCatalogTest, LookupNSSByUUIDForClosedCatalogReturnsFreshestNSS) {
    NamespaceString newNss(nss.db(), "newcol");
    auto newCollUnique = std::make_unique<CollectionMock>(newNss);
    auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(newNss.ns());
    auto newCol = newCollUnique.get();

    catalog.onCloseCatalog(&opCtx);
    catalog.deregisterCollection(colUUID);
    ASSERT(catalog.lookupCollectionByUUID(colUUID) == nullptr);
    ASSERT_EQUALS(*catalog.lookupNSSByUUID(colUUID), nss);
    catalog.registerCollection(colUUID, std::move(newCatalogEntry), std::move(newCollUnique));
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(colUUID), newCol);
    ASSERT_EQUALS(*catalog.lookupNSSByUUID(colUUID), newNss);

    // Ensure that collection still exists after opening the catalog again.
    catalog.onOpenCatalog(&opCtx);
    ASSERT_EQUALS(catalog.lookupCollectionByUUID(colUUID), newCol);
    ASSERT_EQUALS(*catalog.lookupNSSByUUID(colUUID), newNss);
}

DEATH_TEST_F(CollectionCatalogResourceTest, AddInvalidResourceType, "invariant") {
    auto rid = ResourceId(RESOURCE_GLOBAL, 0);
    catalog.addResource(rid, "");
}

TEST_F(CollectionCatalogTest, GetAllCollectionNamesAndGetAllDbNames) {
    NamespaceString aColl("dbA", "collA");
    NamespaceString b1Coll("dbB", "collB1");
    NamespaceString b2Coll("dbB", "collB2");
    NamespaceString cColl("dbC", "collC");
    NamespaceString d1Coll("dbD", "collD1");
    NamespaceString d2Coll("dbD", "collD2");
    NamespaceString d3Coll("dbD", "collD3");

    std::vector<NamespaceString> nsss = {aColl, b1Coll, b2Coll, cColl, d1Coll, d2Coll, d3Coll};
    for (auto& nss : nsss) {
        auto newColl = std::make_unique<CollectionMock>(nss);
        auto newCatalogEntry = std::make_unique<CollectionCatalogEntryMock>(nss.ns());
        auto uuid = CollectionUUID::gen();
        catalog.registerCollection(uuid, std::move(newCatalogEntry), std::move(newColl));
    }

    std::vector<NamespaceString> dCollList = {d1Coll, d2Coll, d3Coll};
    auto res = catalog.getAllCollectionNamesFromDb(&opCtx, "dbD");
    std::sort(res.begin(), res.end());
    ASSERT(res == dCollList);

    std::vector<std::string> dbNames = {"dbA", "dbB", "dbC", "dbD", "testdb"};
    ASSERT(catalog.getAllDbNames() == dbNames);

    catalog.deregisterAllCatalogEntriesAndCollectionObjects();
}

class ForEachCollectionFromDbTest : public CatalogTestFixture {
public:
    void createTestData() {
        CollectionOptions emptyCollOptions;

        CollectionOptions tempCollOptions;
        tempCollOptions.temp = true;

        ASSERT_OK(storageInterface()->createCollection(
            operationContext(), NamespaceString("db", "coll1"), emptyCollOptions));
        ASSERT_OK(storageInterface()->createCollection(
            operationContext(), NamespaceString("db", "coll2"), tempCollOptions));
        ASSERT_OK(storageInterface()->createCollection(
            operationContext(), NamespaceString("db", "coll3"), tempCollOptions));
        ASSERT_OK(storageInterface()->createCollection(
            operationContext(), NamespaceString("db2", "coll4"), emptyCollOptions));
    }
};

TEST_F(ForEachCollectionFromDbTest, ForEachCollectionFromDb) {
    createTestData();
    auto opCtx = operationContext();

    {
        auto dbLock = std::make_unique<Lock::DBLock>(opCtx, "db", MODE_IX);
        int numCollectionsTraversed = 0;
        catalog::forEachCollectionFromDb(
            opCtx,
            "db",
            MODE_X,
            [&](const Collection* collection, const CollectionCatalogEntry* catalogEntry) {
                ASSERT_TRUE(
                    opCtx->lockState()->isCollectionLockedForMode(collection->ns(), MODE_X));
                numCollectionsTraversed++;
                return true;
            });

        ASSERT_EQUALS(numCollectionsTraversed, 3);
    }

    {
        auto dbLock = std::make_unique<Lock::DBLock>(opCtx, "db2", MODE_IX);
        int numCollectionsTraversed = 0;
        catalog::forEachCollectionFromDb(
            opCtx,
            "db2",
            MODE_IS,
            [&](const Collection* collection, const CollectionCatalogEntry* catalogEntry) {
                ASSERT_TRUE(
                    opCtx->lockState()->isCollectionLockedForMode(collection->ns(), MODE_IS));
                numCollectionsTraversed++;
                return true;
            });

        ASSERT_EQUALS(numCollectionsTraversed, 1);
    }

    {
        auto dbLock = std::make_unique<Lock::DBLock>(opCtx, "db3", MODE_IX);
        int numCollectionsTraversed = 0;
        catalog::forEachCollectionFromDb(
            opCtx,
            "db3",
            MODE_S,
            [&](const Collection* collection, const CollectionCatalogEntry* catalogEntry) {
                numCollectionsTraversed++;
                return true;
            });

        ASSERT_EQUALS(numCollectionsTraversed, 0);
    }
}

TEST_F(ForEachCollectionFromDbTest, ForEachCollectionFromDbWithPredicate) {
    createTestData();
    auto opCtx = operationContext();

    {
        auto dbLock = std::make_unique<Lock::DBLock>(opCtx, "db", MODE_IX);
        int numCollectionsTraversed = 0;
        catalog::forEachCollectionFromDb(
            opCtx,
            "db",
            MODE_X,
            [&](const Collection* collection, const CollectionCatalogEntry* catalogEntry) {
                ASSERT_TRUE(
                    opCtx->lockState()->isCollectionLockedForMode(collection->ns(), MODE_X));
                numCollectionsTraversed++;
                return true;
            },
            [&](const Collection* collection, const CollectionCatalogEntry* catalogEntry) {
                ASSERT_TRUE(
                    opCtx->lockState()->isCollectionLockedForMode(collection->ns(), MODE_NONE));
                return DurableCatalog::get(opCtx)
                    ->getCollectionOptions(opCtx, collection->ns())
                    .temp;
            });

        ASSERT_EQUALS(numCollectionsTraversed, 2);
    }

    {
        auto dbLock = std::make_unique<Lock::DBLock>(opCtx, "db", MODE_IX);
        int numCollectionsTraversed = 0;
        catalog::forEachCollectionFromDb(
            opCtx,
            "db",
            MODE_IX,
            [&](const Collection* collection, const CollectionCatalogEntry* catalogEntry) {
                ASSERT_TRUE(
                    opCtx->lockState()->isCollectionLockedForMode(collection->ns(), MODE_IX));
                numCollectionsTraversed++;
                return true;
            },
            [&](const Collection* collection, const CollectionCatalogEntry* catalogEntry) {
                ASSERT_TRUE(
                    opCtx->lockState()->isCollectionLockedForMode(collection->ns(), MODE_NONE));
                return !DurableCatalog::get(opCtx)
                            ->getCollectionOptions(opCtx, collection->ns())
                            .temp;
            });

        ASSERT_EQUALS(numCollectionsTraversed, 1);
    }
}

}  // namespace
