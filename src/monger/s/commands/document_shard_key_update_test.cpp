/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kSharding


#include "monger/platform/basic.h"

#include <string>

#include "monger/s/commands/document_shard_key_update_util.h"

#include "monger/unittest/unittest.h"
#include "monger/util/log.h"


namespace monger {
namespace {

using namespace documentShardKeyUpdateUtil;

class DocumentShardKeyUpdateTest : public unittest::Test {
public:
    DocumentShardKeyUpdateTest() {}
};

TEST_F(DocumentShardKeyUpdateTest, constructShardKeyDeleteCmdObj) {
    NamespaceString nss("test.foo");
    BSONObj updatePreImage = BSON("x" << 4 << "y" << 3 << "_id" << 20);
    int stmtId = 1;

    auto deleteCmdObj = constructShardKeyDeleteCmdObj(nss, updatePreImage, stmtId);

    auto deletesObj = deleteCmdObj["deletes"].Array();
    ASSERT_EQ(deletesObj.size(), 1U);

    auto predicate = deletesObj[0]["q"].Obj();
    ASSERT_EQ(predicate["x"].Int(), 4);
    ASSERT_EQ(predicate["_id"].Int(), 20);

    ASSERT_EQ(deleteCmdObj["delete"].String(), nss.coll());
    ASSERT_EQ(deleteCmdObj["stmtId"].Int(), stmtId);
}

TEST_F(DocumentShardKeyUpdateTest, constructShardKeyInsertCmdObj) {
    NamespaceString nss("test.foo");
    BSONObj updatePostImage = BSON("x" << 4 << "y" << 3 << "_id" << 20);
    int stmtId = 1;

    auto insertCmdObj = constructShardKeyInsertCmdObj(nss, updatePostImage, stmtId);

    auto insertsObj = insertCmdObj["documents"].Array();
    ASSERT_EQ(insertsObj.size(), 1U);

    auto insert = insertsObj[0];
    ASSERT_EQ(insert["x"].Int(), 4);
    ASSERT_EQ(insert["y"].Int(), 3);
    ASSERT_EQ(insert["_id"].Int(), 20);

    ASSERT_EQ(insertCmdObj["insert"].String(), nss.coll());
    ASSERT_EQ(insertCmdObj["stmtId"].Int(), stmtId);
}
}  // namespace
}  // namespace monger
