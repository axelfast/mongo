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

#include <functional>
#include <limits>
#include <string>

#include "monger/db/jsobj.h"
#include "monger/db/repl/bson_extract_optime.h"
#include "monger/db/repl/optime.h"
#include "monger/unittest/unittest.h"

using namespace monger;

TEST(ExtractBSON, ExtractOpTimeField) {
    // Outer object cases.
    BSONObj obj = BSON("a" << BSON("ts" << Timestamp(10, 0) << "t" << 2) << "b"
                           << "notAnObj");
    repl::OpTime opTime;
    ASSERT_OK(bsonExtractOpTimeField(obj, "a", &opTime));
    ASSERT(repl::OpTime(Timestamp(10, 0), 2) == opTime);
    ASSERT_EQUALS(ErrorCodes::TypeMismatch, bsonExtractOpTimeField(obj, "b", &opTime));
    ASSERT_EQUALS(ErrorCodes::NoSuchKey, bsonExtractOpTimeField(obj, "c", &opTime));

    // Missing timestamp field.
    obj = BSON("a" << BSON("ts"
                           << "notATimestamp"
                           << "t"
                           << 2));
    ASSERT_EQUALS(ErrorCodes::TypeMismatch, bsonExtractOpTimeField(obj, "a", &opTime));
    // Wrong typed timestamp field.
    obj = BSON("a" << BSON("t" << 2));
    ASSERT_EQUALS(ErrorCodes::NoSuchKey, bsonExtractOpTimeField(obj, "a", &opTime));
    // Missing term field.
    obj = BSON("a" << BSON("ts" << Timestamp(10, 0) << "t"
                                << "notANumber"));
    ASSERT_EQUALS(ErrorCodes::TypeMismatch, bsonExtractOpTimeField(obj, "a", &opTime));
    // Wrong typed term field.
    obj = BSON("a" << BSON("ts" << Timestamp(10, 0)));
    ASSERT_EQUALS(ErrorCodes::NoSuchKey, bsonExtractOpTimeField(obj, "a", &opTime));
}
