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

#include "monger/bson/bsonmisc.h"
#include "monger/bson/bsonobjbuilder.h"
#include "monger/s/request_types/commit_chunk_migration_request_type.h"
#include "monger/unittest/unittest.h"

namespace monger {

using unittest::assertGet;

namespace {

const auto kNamespaceString = NamespaceString("TestDB", "TestColl");

const auto kShardId0 = ShardId("shard0");
const auto kShardId1 = ShardId("shard1");

const auto kKey0 = BSON("Key" << -100);
const auto kKey1 = BSON("Key" << 100);
const auto kKey2 = BSON("Key" << -50);
const auto kKey3 = BSON("Key" << 50);

const char kConfigSvrCommitChunkMigration[] = "_configsvrCommitChunkMigration";

TEST(CommitChunkMigrationRequest, WithoutControlChunk) {
    BSONObjBuilder builder;

    ChunkType migratedChunk;
    migratedChunk.setMin(kKey0);
    migratedChunk.setMax(kKey1);

    ChunkVersion fromShardCollectionVersion(1, 2, OID::gen());

    Timestamp validAfter{1};

    CommitChunkMigrationRequest::appendAsCommand(&builder,
                                                 kNamespaceString,
                                                 kShardId0,
                                                 kShardId1,
                                                 migratedChunk,
                                                 fromShardCollectionVersion,
                                                 validAfter);

    BSONObj cmdObj = builder.obj();

    auto request = assertGet(CommitChunkMigrationRequest::createFromCommand(
        NamespaceString(cmdObj[kConfigSvrCommitChunkMigration].String()), cmdObj));

    ASSERT_EQ(kNamespaceString, request.getNss());
    ASSERT_EQ(kShardId0, request.getFromShard());
    ASSERT_EQ(kShardId1, request.getToShard());
    ASSERT_BSONOBJ_EQ(kKey0, request.getMigratedChunk().getMin());
    ASSERT_BSONOBJ_EQ(kKey1, request.getMigratedChunk().getMax());
    ASSERT_EQ(fromShardCollectionVersion.epoch(), request.getCollectionEpoch());
}

}  // namespace
}  // namespace monger
