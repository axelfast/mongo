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

#include "monger/s/query/store_possible_cursor.h"

#include "monger/bson/json.h"
#include "monger/db/query/cursor_response.h"
#include "monger/db/query/query_test_service_context.h"
#include "monger/s/query/cluster_cursor_manager.h"
#include "monger/unittest/unittest.h"
#include "monger/util/clock_source_mock.h"
#include "monger/util/net/hostandport.h"

namespace monger {
namespace {

const NamespaceString nss("test.collection");
const HostAndPort hostAndPort("testhost", 27017);
const ShardId shardId("testshard");

class StorePossibleCursorTest : public unittest::Test {
protected:
    StorePossibleCursorTest() : _manager(&_clockSourceMock) {
        _opCtx = _serviceContext.makeOperationContext();
    }

    OperationContext* opCtx() const {
        return _opCtx.get();
    }

    ClusterCursorManager* getManager() {
        return &_manager;
    }

private:
    QueryTestServiceContext _serviceContext;
    ServiceContext::UniqueOperationContext _opCtx;
    ClockSourceMock _clockSourceMock;
    ClusterCursorManager _manager;
};

// Test that storePossibleCursor() returns a valid cursor response document.
TEST_F(StorePossibleCursorTest, ReturnsValidCursorResponse) {
    std::vector<BSONObj> batch = {fromjson("{_id: 1}"), fromjson("{_id: 2}")};
    CursorResponse cursorResponse(nss, CursorId(0), batch);
    auto outgoingCursorResponse =
        storePossibleCursor(opCtx(),
                            shardId,
                            hostAndPort,
                            cursorResponse.toBSON(CursorResponse::ResponseType::InitialResponse),
                            nss,
                            nullptr,  // TaskExecutor
                            getManager(),
                            PrivilegeVector());
    ASSERT_OK(outgoingCursorResponse.getStatus());

    auto parsedOutgoingResponse = CursorResponse::parseFromBSON(outgoingCursorResponse.getValue());
    ASSERT_OK(parsedOutgoingResponse.getStatus());
    ASSERT_EQ(nss.toString(), parsedOutgoingResponse.getValue().getNSS().toString());
    ASSERT_EQ(0U, parsedOutgoingResponse.getValue().getCursorId());
    ASSERT_EQ(2U, parsedOutgoingResponse.getValue().getBatch().size());
    ASSERT_BSONOBJ_EQ(fromjson("{_id: 1}"), parsedOutgoingResponse.getValue().getBatch()[0]);
    ASSERT_BSONOBJ_EQ(fromjson("{_id: 2}"), parsedOutgoingResponse.getValue().getBatch()[1]);
}

// Test that storePossibleCursor() propagates an error if it cannot parse the cursor response.
TEST_F(StorePossibleCursorTest, FailsGracefullyOnBadCursorResponseDocument) {
    auto outgoingCursorResponse = storePossibleCursor(opCtx(),
                                                      shardId,
                                                      hostAndPort,
                                                      fromjson("{ok: 1, cursor: {}}"),
                                                      nss,
                                                      nullptr,  // TaskExecutor
                                                      getManager(),
                                                      PrivilegeVector());
    ASSERT_NOT_OK(outgoingCursorResponse.getStatus());
    ASSERT_EQ(ErrorCodes::TypeMismatch, outgoingCursorResponse.getStatus());
}

// Test that storePossibleCursor() passes up the command response if it is not recognized as a
// cursor response.
TEST_F(StorePossibleCursorTest, PassesUpCommandResultIfItDoesNotDescribeACursor) {
    BSONObj notACursorObj = BSON("not"
                                 << "cursor");
    auto outgoingCursorResponse = storePossibleCursor(opCtx(),
                                                      shardId,
                                                      hostAndPort,
                                                      notACursorObj,
                                                      nss,
                                                      nullptr,  // TaskExecutor
                                                      getManager(),
                                                      PrivilegeVector());
    ASSERT_OK(outgoingCursorResponse.getStatus());
    ASSERT_BSONOBJ_EQ(notACursorObj, outgoingCursorResponse.getValue());
}

}  // namespace
}  // namespace monger
