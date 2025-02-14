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

#include "monger/unittest/unittest.h"

#include "monger/db/matcher/expression_parser.h"

#include "monger/db/jsobj.h"
#include "monger/db/json.h"
#include "monger/db/matcher/expression.h"
#include "monger/db/matcher/expression_leaf.h"
#include "monger/db/pipeline/expression_context_for_test.h"

namespace monger {

TEST(MatchExpressionParserTreeTest, OR1) {
    BSONObj query = BSON("$or" << BSON_ARRAY(BSON("x" << 1) << BSON("y" << 2)));
    boost::intrusive_ptr<ExpressionContextForTest> expCtx(new ExpressionContextForTest());
    StatusWithMatchExpression result = MatchExpressionParser::parse(query, expCtx);
    ASSERT_TRUE(result.isOK());

    ASSERT(result.getValue()->matchesBSON(BSON("x" << 1)));
    ASSERT(result.getValue()->matchesBSON(BSON("y" << 2)));
    ASSERT(!result.getValue()->matchesBSON(BSON("x" << 3)));
    ASSERT(!result.getValue()->matchesBSON(BSON("y" << 1)));
}

TEST(MatchExpressionParserTreeTest, OREmbedded) {
    BSONObj query1 = BSON("$or" << BSON_ARRAY(BSON("x" << 1) << BSON("y" << 2)));
    BSONObj query2 = BSON("$or" << BSON_ARRAY(query1));
    boost::intrusive_ptr<ExpressionContextForTest> expCtx(new ExpressionContextForTest());
    StatusWithMatchExpression result = MatchExpressionParser::parse(query2, expCtx);
    ASSERT_TRUE(result.isOK());

    ASSERT(result.getValue()->matchesBSON(BSON("x" << 1)));
    ASSERT(result.getValue()->matchesBSON(BSON("y" << 2)));
    ASSERT(!result.getValue()->matchesBSON(BSON("x" << 3)));
    ASSERT(!result.getValue()->matchesBSON(BSON("y" << 1)));
}


TEST(MatchExpressionParserTreeTest, AND1) {
    BSONObj query = BSON("$and" << BSON_ARRAY(BSON("x" << 1) << BSON("y" << 2)));
    boost::intrusive_ptr<ExpressionContextForTest> expCtx(new ExpressionContextForTest());
    StatusWithMatchExpression result = MatchExpressionParser::parse(query, expCtx);
    ASSERT_TRUE(result.isOK());

    ASSERT(!result.getValue()->matchesBSON(BSON("x" << 1)));
    ASSERT(!result.getValue()->matchesBSON(BSON("y" << 2)));
    ASSERT(!result.getValue()->matchesBSON(BSON("x" << 3)));
    ASSERT(!result.getValue()->matchesBSON(BSON("y" << 1)));
    ASSERT(result.getValue()->matchesBSON(BSON("x" << 1 << "y" << 2)));
    ASSERT(!result.getValue()->matchesBSON(BSON("x" << 2 << "y" << 2)));
}

TEST(MatchExpressionParserTreeTest, NOREmbedded) {
    BSONObj query = BSON("$nor" << BSON_ARRAY(BSON("x" << 1) << BSON("y" << 2)));
    boost::intrusive_ptr<ExpressionContextForTest> expCtx(new ExpressionContextForTest());
    StatusWithMatchExpression result = MatchExpressionParser::parse(query, expCtx);
    ASSERT_TRUE(result.isOK());

    ASSERT(!result.getValue()->matchesBSON(BSON("x" << 1)));
    ASSERT(!result.getValue()->matchesBSON(BSON("y" << 2)));
    ASSERT(result.getValue()->matchesBSON(BSON("x" << 3)));
    ASSERT(result.getValue()->matchesBSON(BSON("y" << 1)));
}

TEST(MatchExpressionParserTreeTest, NOT1) {
    BSONObj query = BSON("x" << BSON("$not" << BSON("$gt" << 5)));
    boost::intrusive_ptr<ExpressionContextForTest> expCtx(new ExpressionContextForTest());
    StatusWithMatchExpression result = MatchExpressionParser::parse(query, expCtx);
    ASSERT_TRUE(result.isOK());

    ASSERT(result.getValue()->matchesBSON(BSON("x" << 2)));
    ASSERT(!result.getValue()->matchesBSON(BSON("x" << 8)));
}

TEST(MatchExpressionParserLeafTest, NotRegex1) {
    BSONObjBuilder b;
    b.appendRegex("$not", "abc", "i");
    BSONObj query = BSON("x" << b.obj());
    boost::intrusive_ptr<ExpressionContextForTest> expCtx(new ExpressionContextForTest());
    StatusWithMatchExpression result = MatchExpressionParser::parse(query, expCtx);
    ASSERT_TRUE(result.isOK());

    ASSERT(!result.getValue()->matchesBSON(BSON("x"
                                                << "abc")));
    ASSERT(!result.getValue()->matchesBSON(BSON("x"
                                                << "ABC")));
    ASSERT(result.getValue()->matchesBSON(BSON("x"
                                               << "AC")));
}
}
