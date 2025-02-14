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

#include "monger/client/read_preference.h"
#include "monger/db/namespace_string.h"
#include "monger/db/s/config/sharding_catalog_manager.h"
#include "monger/s/catalog/sharding_catalog_client.h"
#include "monger/s/catalog/type_chunk.h"
#include "monger/s/config_server_test_fixture.h"

namespace monger {
namespace {

const NamespaceString kNamespace("TestDB.TestColl");

using MergeChunkTest = ConfigServerTestFixture;

TEST_F(MergeChunkTest, MergeExistingChunksCorrectlyShouldSucceed) {
    ChunkType chunk;
    chunk.setNS(kNamespace);

    auto origVersion = ChunkVersion(1, 0, OID::gen());
    chunk.setVersion(origVersion);
    chunk.setShard(ShardId("shard0000"));

    // Construct chunk to be merged
    auto chunk2(chunk);

    auto chunkMin = BSON("a" << 1);
    auto chunkBound = BSON("a" << 5);
    auto chunkMax = BSON("a" << 10);
    // first chunk boundaries
    chunk.setMin(chunkMin);
    chunk.setMax(chunkBound);
    // second chunk boundaries
    chunk2.setMin(chunkBound);
    chunk2.setMax(chunkMax);

    std::vector<BSONObj> chunkBoundaries{chunkMin, chunkBound, chunkMax};

    setupChunks({chunk, chunk2});

    Timestamp validAfter{100, 0};

    ASSERT_OK(ShardingCatalogManager::get(operationContext())
                  ->commitChunkMerge(operationContext(),
                                     kNamespace,
                                     origVersion.epoch(),
                                     chunkBoundaries,
                                     "shard0000",
                                     validAfter));

    auto findResponse = uassertStatusOK(
        getConfigShard()->exhaustiveFindOnConfig(operationContext(),
                                                 ReadPreferenceSetting{ReadPreference::PrimaryOnly},
                                                 repl::ReadConcernLevel::kLocalReadConcern,
                                                 ChunkType::ConfigNS,
                                                 BSON(ChunkType::ns() << "TestDB.TestColl"),
                                                 BSON(ChunkType::lastmod << -1),
                                                 boost::none));

    const auto& chunksVector = findResponse.docs;

    // There should be exactly one chunk left in the collection
    ASSERT_EQ(1u, chunksVector.size());

    // MergedChunk should have range [chunkMin, chunkMax]
    auto mergedChunk = uassertStatusOK(ChunkType::fromConfigBSON(chunksVector.front()));
    ASSERT_BSONOBJ_EQ(chunkMin, mergedChunk.getMin());
    ASSERT_BSONOBJ_EQ(chunkMax, mergedChunk.getMax());

    {
        // Check for increment on mergedChunk's minor version
        ASSERT_EQ(origVersion.majorVersion(), mergedChunk.getVersion().majorVersion());
        ASSERT_EQ(origVersion.minorVersion() + 1, mergedChunk.getVersion().minorVersion());
    }

    // Make sure history is there
    ASSERT_EQ(1UL, mergedChunk.getHistory().size());
    ASSERT_EQ(validAfter, mergedChunk.getHistory().front().getValidAfter());
}

TEST_F(MergeChunkTest, MergeSeveralChunksCorrectlyShouldSucceed) {
    ChunkType chunk;
    chunk.setNS(kNamespace);

    auto origVersion = ChunkVersion(1, 0, OID::gen());
    chunk.setVersion(origVersion);
    chunk.setShard(ShardId("shard0000"));

    // Construct chunks to be merged
    auto chunk2(chunk);
    auto chunk3(chunk);

    auto chunkMin = BSON("a" << 1);
    auto chunkBound = BSON("a" << 5);
    auto chunkBound2 = BSON("a" << 7);
    auto chunkMax = BSON("a" << 10);
    // first chunk boundaries
    chunk.setMin(chunkMin);
    chunk.setMax(chunkBound);
    // second chunk boundaries
    chunk2.setMin(chunkBound);
    chunk2.setMax(chunkBound2);
    // third chunk boundaries
    chunk3.setMin(chunkBound2);
    chunk3.setMax(chunkMax);

    // Record chunk boundaries for passing into commitChunkMerge
    std::vector<BSONObj> chunkBoundaries{chunkMin, chunkBound, chunkBound2, chunkMax};

    setupChunks({chunk, chunk2, chunk3});

    Timestamp validAfter{100, 0};

    ASSERT_OK(ShardingCatalogManager::get(operationContext())
                  ->commitChunkMerge(operationContext(),
                                     kNamespace,
                                     origVersion.epoch(),
                                     chunkBoundaries,
                                     "shard0000",
                                     validAfter));

    auto findResponse = uassertStatusOK(
        getConfigShard()->exhaustiveFindOnConfig(operationContext(),
                                                 ReadPreferenceSetting{ReadPreference::PrimaryOnly},
                                                 repl::ReadConcernLevel::kLocalReadConcern,
                                                 ChunkType::ConfigNS,
                                                 BSON(ChunkType::ns() << "TestDB.TestColl"),
                                                 BSON(ChunkType::lastmod << -1),
                                                 boost::none));

    const auto& chunksVector = findResponse.docs;

    // There should be exactly one chunk left in the collection
    ASSERT_EQ(1u, chunksVector.size());

    // MergedChunk should have range [chunkMin, chunkMax]
    auto mergedChunk = uassertStatusOK(ChunkType::fromConfigBSON(chunksVector.front()));
    ASSERT_BSONOBJ_EQ(chunkMin, mergedChunk.getMin());
    ASSERT_BSONOBJ_EQ(chunkMax, mergedChunk.getMax());

    {
        // Check for increment on mergedChunk's minor version
        ASSERT_EQ(origVersion.majorVersion(), mergedChunk.getVersion().majorVersion());
        ASSERT_EQ(origVersion.minorVersion() + 1, mergedChunk.getVersion().minorVersion());
    }

    // Make sure history is there
    ASSERT_EQ(1UL, mergedChunk.getHistory().size());
    ASSERT_EQ(validAfter, mergedChunk.getHistory().front().getValidAfter());
}

TEST_F(MergeChunkTest, NewMergeShouldClaimHighestVersion) {
    ChunkType chunk, otherChunk;
    chunk.setNS(kNamespace);
    otherChunk.setNS(kNamespace);
    auto collEpoch = OID::gen();

    auto origVersion = ChunkVersion(1, 2, collEpoch);
    chunk.setVersion(origVersion);
    chunk.setShard(ShardId("shard0000"));

    // Construct chunk to be merged
    auto chunk2(chunk);

    auto chunkMin = BSON("a" << 1);
    auto chunkBound = BSON("a" << 5);
    auto chunkMax = BSON("a" << 10);
    // first chunk boundaries
    chunk.setMin(chunkMin);
    chunk.setMax(chunkBound);
    // second chunk boundaries
    chunk2.setMin(chunkBound);
    chunk2.setMax(chunkMax);

    // Record chunk boundaries for passing into commitChunkMerge
    std::vector<BSONObj> chunkBoundaries{chunkMin, chunkBound, chunkMax};

    // Set up other chunk with competing version
    auto competingVersion = ChunkVersion(2, 1, collEpoch);
    otherChunk.setVersion(competingVersion);
    otherChunk.setShard(ShardId("shard0000"));
    otherChunk.setMin(BSON("a" << 10));
    otherChunk.setMax(BSON("a" << 20));

    setupChunks({chunk, chunk2, otherChunk});

    Timestamp validAfter{100, 0};

    ASSERT_OK(ShardingCatalogManager::get(operationContext())
                  ->commitChunkMerge(operationContext(),
                                     kNamespace,
                                     collEpoch,
                                     chunkBoundaries,
                                     "shard0000",
                                     validAfter));

    auto findResponse = uassertStatusOK(
        getConfigShard()->exhaustiveFindOnConfig(operationContext(),
                                                 ReadPreferenceSetting{ReadPreference::PrimaryOnly},
                                                 repl::ReadConcernLevel::kLocalReadConcern,
                                                 ChunkType::ConfigNS,
                                                 BSON(ChunkType::ns() << "TestDB.TestColl"),
                                                 BSON(ChunkType::lastmod << -1),
                                                 boost::none));

    const auto& chunksVector = findResponse.docs;

    // There should be exactly two chunks left in the collection: one merged, one competing
    ASSERT_EQ(2u, chunksVector.size());

    // MergedChunk should have range [chunkMin, chunkMax]
    auto mergedChunk = uassertStatusOK(ChunkType::fromConfigBSON(chunksVector.front()));
    ASSERT_BSONOBJ_EQ(chunkMin, mergedChunk.getMin());
    ASSERT_BSONOBJ_EQ(chunkMax, mergedChunk.getMax());

    {
        // Check for minor increment on collection version
        ASSERT_EQ(competingVersion.majorVersion(), mergedChunk.getVersion().majorVersion());
        ASSERT_EQ(competingVersion.minorVersion() + 1, mergedChunk.getVersion().minorVersion());
    }

    // Make sure history is there
    ASSERT_EQ(1UL, mergedChunk.getHistory().size());
    ASSERT_EQ(validAfter, mergedChunk.getHistory().front().getValidAfter());
}

TEST_F(MergeChunkTest, MergeLeavesOtherChunksAlone) {
    ChunkType chunk;
    chunk.setNS(kNamespace);

    auto origVersion = ChunkVersion(1, 2, OID::gen());
    chunk.setVersion(origVersion);
    chunk.setShard(ShardId("shard0000"));

    // Construct chunk to be merged
    auto chunk2(chunk);

    auto chunkMin = BSON("a" << 1);
    auto chunkBound = BSON("a" << 5);
    auto chunkMax = BSON("a" << 10);
    // first chunk boundaries
    chunk.setMin(chunkMin);
    chunk.setMax(chunkBound);
    // second chunk boundaries
    chunk2.setMin(chunkBound);
    chunk2.setMax(chunkMax);

    // Record chunk boundaries for passing into commitChunkMerge
    std::vector<BSONObj> chunkBoundaries{chunkMin, chunkBound, chunkMax};

    // Set up unmerged chunk
    auto otherChunk(chunk);
    otherChunk.setMin(BSON("a" << 10));
    otherChunk.setMax(BSON("a" << 20));

    setupChunks({chunk, chunk2, otherChunk});

    Timestamp validAfter{1};

    ASSERT_OK(ShardingCatalogManager::get(operationContext())
                  ->commitChunkMerge(operationContext(),
                                     kNamespace,
                                     origVersion.epoch(),
                                     chunkBoundaries,
                                     "shard0000",
                                     validAfter));

    auto findResponse = uassertStatusOK(
        getConfigShard()->exhaustiveFindOnConfig(operationContext(),
                                                 ReadPreferenceSetting{ReadPreference::PrimaryOnly},
                                                 repl::ReadConcernLevel::kLocalReadConcern,
                                                 ChunkType::ConfigNS,
                                                 BSON(ChunkType::ns() << "TestDB.TestColl"),
                                                 BSON(ChunkType::lastmod << -1),
                                                 boost::none));

    const auto& chunksVector = findResponse.docs;

    // There should be exactly two chunks left in the collection: one merged, one untouched
    ASSERT_EQ(2u, chunksVector.size());

    // MergedChunk should have range [chunkMin, chunkMax]
    auto mergedChunk = uassertStatusOK(ChunkType::fromConfigBSON(chunksVector.front()));
    ASSERT_BSONOBJ_EQ(chunkMin, mergedChunk.getMin());
    ASSERT_BSONOBJ_EQ(chunkMax, mergedChunk.getMax());

    {
        // Check for increment on mergedChunk's minor version
        ASSERT_EQ(origVersion.majorVersion(), mergedChunk.getVersion().majorVersion());
        ASSERT_EQ(origVersion.minorVersion() + 1, mergedChunk.getVersion().minorVersion());
    }

    // OtherChunk should have been left alone
    auto foundOtherChunk = uassertStatusOK(ChunkType::fromConfigBSON(chunksVector.back()));
    ASSERT_BSONOBJ_EQ(otherChunk.getMin(), foundOtherChunk.getMin());
    ASSERT_BSONOBJ_EQ(otherChunk.getMax(), foundOtherChunk.getMax());
}

TEST_F(MergeChunkTest, NonExistingNamespace) {
    ChunkType chunk;
    chunk.setNS(kNamespace);

    auto origVersion = ChunkVersion(1, 0, OID::gen());
    chunk.setVersion(origVersion);
    chunk.setShard(ShardId("shard0000"));

    // Construct chunk to be merged
    auto chunk2(chunk);

    auto chunkMin = BSON("a" << 1);
    auto chunkBound = BSON("a" << 5);
    auto chunkMax = BSON("a" << 10);
    // first chunk boundaries
    chunk.setMin(chunkMin);
    chunk.setMax(chunkBound);
    chunk2.setMin(chunkBound);
    chunk2.setMax(chunkMax);

    // Record chunk boundaries for passing into commitChunkMerge
    std::vector<BSONObj> chunkBoundaries{chunkMin, chunkBound, chunkMax};

    setupChunks({chunk, chunk2});

    Timestamp validAfter{1};

    auto mergeStatus = ShardingCatalogManager::get(operationContext())
                           ->commitChunkMerge(operationContext(),
                                              NamespaceString("TestDB.NonExistingColl"),
                                              origVersion.epoch(),
                                              chunkBoundaries,
                                              "shard0000",
                                              validAfter);
    ASSERT_EQ(ErrorCodes::IllegalOperation, mergeStatus);
}

TEST_F(MergeChunkTest, NonMatchingEpochsOfChunkAndRequestErrors) {
    ChunkType chunk;
    chunk.setNS(kNamespace);

    auto origVersion = ChunkVersion(1, 0, OID::gen());
    chunk.setVersion(origVersion);
    chunk.setShard(ShardId("shard0000"));

    // Construct chunk to be merged
    auto chunk2(chunk);

    auto chunkMin = BSON("a" << 1);
    auto chunkBound = BSON("a" << 5);
    auto chunkMax = BSON("a" << 10);
    // first chunk boundaries
    chunk.setMin(chunkMin);
    chunk.setMax(chunkBound);
    chunk2.setMin(chunkBound);
    chunk2.setMax(chunkMax);

    // Record chunk baoundaries for passing into commitChunkMerge
    std::vector<BSONObj> chunkBoundaries{chunkMin, chunkBound, chunkMax};

    setupChunks({chunk, chunk2});

    Timestamp validAfter{1};

    auto mergeStatus = ShardingCatalogManager::get(operationContext())
                           ->commitChunkMerge(operationContext(),
                                              kNamespace,
                                              OID::gen(),
                                              chunkBoundaries,
                                              "shard0000",
                                              validAfter);
    ASSERT_EQ(ErrorCodes::StaleEpoch, mergeStatus);
}

TEST_F(MergeChunkTest, MergeAlreadyHappenedFailsPrecondition) {
    ChunkType chunk;
    chunk.setNS(kNamespace);

    auto origVersion = ChunkVersion(1, 0, OID::gen());
    chunk.setVersion(origVersion);
    chunk.setShard(ShardId("shard0000"));

    // Construct chunk to be merged
    auto chunk2(chunk);

    auto chunkMin = BSON("a" << 1);
    auto chunkBound = BSON("a" << 5);
    auto chunkMax = BSON("a" << 10);
    // first chunk boundaries
    chunk.setMin(chunkMin);
    chunk.setMax(chunkBound);
    // second chunk boundaries
    chunk2.setMin(chunkBound);
    chunk2.setMax(chunkMax);

    std::vector<BSONObj> chunkBoundaries{chunkMin, chunkBound, chunkMax};

    ChunkType mergedChunk(chunk);
    auto mergedVersion = chunk.getVersion();
    mergedVersion.incMinor();
    mergedChunk.setVersion(mergedVersion);
    mergedChunk.setMax(chunkMax);

    setupChunks({mergedChunk});

    Timestamp validAfter{1};

    ASSERT_EQ(ErrorCodes::BadValue,
              ShardingCatalogManager::get(operationContext())
                  ->commitChunkMerge(operationContext(),
                                     kNamespace,
                                     origVersion.epoch(),
                                     chunkBoundaries,
                                     "shard0000",
                                     validAfter));

    // Verify that no change to config.chunks happened.
    auto findResponse = uassertStatusOK(
        getConfigShard()->exhaustiveFindOnConfig(operationContext(),
                                                 ReadPreferenceSetting{ReadPreference::PrimaryOnly},
                                                 repl::ReadConcernLevel::kLocalReadConcern,
                                                 ChunkType::ConfigNS,
                                                 BSON(ChunkType::ns() << "TestDB.TestColl"),
                                                 BSON(ChunkType::lastmod << -1),
                                                 boost::none));

    const auto& chunksVector = findResponse.docs;

    // There should be exactly one chunk left in the collection
    ASSERT_EQ(1u, chunksVector.size());

    // MergedChunk should have range [chunkMin, chunkMax]
    ChunkType foundChunk = uassertStatusOK(ChunkType::fromConfigBSON(chunksVector.front()));
    ASSERT_BSONOBJ_EQ(mergedChunk.toConfigBSON(), foundChunk.toConfigBSON());
}

TEST_F(MergeChunkTest, ChunkBoundariesOutOfOrderFails) {
    const OID epoch = OID::gen();
    const std::vector<BSONObj> chunkBoundaries{
        BSON("a" << 100), BSON("a" << 200), BSON("a" << 30), BSON("a" << 400)};

    {
        std::vector<ChunkType> originalChunks;
        ChunkVersion version = ChunkVersion(1, 0, epoch);

        ChunkType chunk;
        chunk.setNS(kNamespace);
        chunk.setShard(ShardId("shard0000"));

        chunk.setVersion(version);
        chunk.setMin(BSON("a" << 100));
        chunk.setMax(BSON("a" << 200));
        originalChunks.push_back(chunk);

        version.incMinor();
        chunk.setMin(BSON("a" << 200));
        chunk.setMax(BSON("a" << 300));
        chunk.setVersion(version);
        originalChunks.push_back(chunk);

        version.incMinor();
        chunk.setMin(BSON("a" << 300));
        chunk.setMax(BSON("a" << 400));
        chunk.setVersion(version);
        originalChunks.push_back(chunk);

        setupChunks(originalChunks);
    }

    Timestamp validAfter{1};

    ASSERT_EQ(
        ErrorCodes::InvalidOptions,
        ShardingCatalogManager::get(operationContext())
            ->commitChunkMerge(
                operationContext(), kNamespace, epoch, chunkBoundaries, "shard0000", validAfter));
}

TEST_F(MergeChunkTest, MergingChunksWithDollarPrefixShouldSucceed) {
    ChunkType chunk1;
    chunk1.setNS(kNamespace);

    auto origVersion = ChunkVersion(1, 0, OID::gen());
    chunk1.setVersion(origVersion);
    chunk1.setShard(ShardId("shard0000"));

    auto chunk2(chunk1);
    auto chunk3(chunk1);

    auto chunkMin = BSON("a" << kMinBSONKey);
    auto chunkBound1 = BSON("a" << BSON("$maxKey" << 1));
    auto chunkBound2 = BSON("a" << BSON("$mixKey" << 1));
    auto chunkMax = BSON("a" << kMaxBSONKey);

    // first chunk boundaries
    chunk1.setMin(chunkMin);
    chunk1.setMax(chunkBound1);
    // second chunk boundaries
    chunk2.setMin(chunkBound1);
    chunk2.setMax(chunkBound2);
    // third chunk boundaries
    chunk3.setMin(chunkBound2);
    chunk3.setMax(chunkMax);

    setupChunks({chunk1, chunk2, chunk3});

    // Record chunk boundaries for passing into commitChunkMerge
    std::vector<BSONObj> chunkBoundaries{chunkMin, chunkBound1, chunkBound2, chunkMax};
    Timestamp validAfter{100, 0};

    ASSERT_OK(ShardingCatalogManager::get(operationContext())
                  ->commitChunkMerge(operationContext(),
                                     kNamespace,
                                     origVersion.epoch(),
                                     chunkBoundaries,
                                     "shard0000",
                                     validAfter));

    auto findResponse = uassertStatusOK(
        getConfigShard()->exhaustiveFindOnConfig(operationContext(),
                                                 ReadPreferenceSetting{ReadPreference::PrimaryOnly},
                                                 repl::ReadConcernLevel::kLocalReadConcern,
                                                 ChunkType::ConfigNS,
                                                 BSON(ChunkType::ns() << "TestDB.TestColl"),
                                                 BSON(ChunkType::lastmod << -1),
                                                 boost::none));

    const auto& chunksVector = findResponse.docs;

    // There should be exactly one chunk left in the collection
    ASSERT_EQ(1u, chunksVector.size());

    // MergedChunk should have range [chunkMin, chunkMax]
    auto mergedChunk = uassertStatusOK(ChunkType::fromConfigBSON(chunksVector.front()));
    ASSERT_BSONOBJ_EQ(chunkMin, mergedChunk.getMin());
    ASSERT_BSONOBJ_EQ(chunkMax, mergedChunk.getMax());

    {
        // Check for increment on mergedChunk's minor version
        ASSERT_EQ(origVersion.majorVersion(), mergedChunk.getVersion().majorVersion());
        ASSERT_EQ(origVersion.minorVersion() + 1, mergedChunk.getVersion().minorVersion());
    }

    // Make sure history is there
    ASSERT_EQ(1UL, mergedChunk.getHistory().size());
    ASSERT_EQ(validAfter, mergedChunk.getHistory().front().getValidAfter());
}

}  // namespace
}  // namespace monger
