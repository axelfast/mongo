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

#include "monger/db/jsobj.h"
#include "monger/rpc/metadata/oplog_query_metadata.h"
#include "monger/unittest/unittest.h"

namespace monger {
namespace rpc {
namespace {

using repl::OpTime;

TEST(ReplResponseMetadataTest, OplogQueryMetadataRoundtrip) {
    OpTime opTime1(Timestamp(1234, 100), 5);
    Date_t committedWall = Date_t() + Seconds(opTime1.getSecs());
    OpTime opTime2(Timestamp(7777, 101), 6);
    OplogQueryMetadata metadata({opTime1, committedWall}, opTime2, 6, 12, -1);

    ASSERT_EQ(opTime1, metadata.getLastOpCommitted().opTime);
    ASSERT_EQ(committedWall, metadata.getLastOpCommitted().wallTime);
    ASSERT_EQ(opTime2, metadata.getLastOpApplied());

    BSONObjBuilder builder;
    metadata.writeToMetadata(&builder).transitional_ignore();

    BSONObj expectedObj(BSON(kOplogQueryMetadataFieldName << BSON(
                                 "lastOpCommitted"
                                 << BSON("ts" << opTime1.getTimestamp() << "t" << opTime1.getTerm())
                                 << "lastCommittedWall"
                                 << committedWall
                                 << "lastOpApplied"
                                 << BSON("ts" << opTime2.getTimestamp() << "t" << opTime2.getTerm())
                                 << "rbid"
                                 << 6
                                 << "primaryIndex"
                                 << 12
                                 << "syncSourceIndex"
                                 << -1)));

    BSONObj serializedObj = builder.obj();
    ASSERT_BSONOBJ_EQ(expectedObj, serializedObj);

    auto cloneStatus =
        OplogQueryMetadata::readFromMetadata(serializedObj, /*requireWallTime*/ true);
    ASSERT_OK(cloneStatus.getStatus());

    const auto& clonedMetadata = cloneStatus.getValue();
    ASSERT_EQ(opTime1, clonedMetadata.getLastOpCommitted().opTime);
    ASSERT_EQ(opTime2, clonedMetadata.getLastOpApplied());
    ASSERT_EQ(committedWall, clonedMetadata.getLastOpCommitted().wallTime);
    ASSERT_EQ(metadata.getRBID(), clonedMetadata.getRBID());
    ASSERT_EQ(metadata.getPrimaryIndex(), clonedMetadata.getPrimaryIndex());
    ASSERT_EQ(metadata.getSyncSourceIndex(), clonedMetadata.getSyncSourceIndex());

    BSONObjBuilder clonedBuilder;
    clonedMetadata.writeToMetadata(&clonedBuilder).transitional_ignore();

    BSONObj clonedSerializedObj = clonedBuilder.obj();
    ASSERT_BSONOBJ_EQ(expectedObj, clonedSerializedObj);
}

}  // unnamed namespace
}  // namespace rpc
}  // namespace monger
