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
#include "monger/db/json.h"
#include "monger/db/matcher/expression.h"
#include "monger/db/matcher/expression_array.h"
#include "monger/db/matcher/expression_leaf.h"
#include "monger/db/matcher/expression_tree.h"
#include "monger/db/query/collation/collator_interface_mock.h"
#include "monger/unittest/unittest.h"

namespace monger {

using std::unique_ptr;

TEST(ElemMatchObjectMatchExpression, MatchesElementSingle) {
    BSONObj baseOperand = BSON("b" << 5);
    BSONObj match = BSON("a" << BSON_ARRAY(BSON("b" << 5.0)));
    BSONObj notMatch = BSON("a" << BSON_ARRAY(BSON("b" << 6)));
    unique_ptr<ComparisonMatchExpression> eq(new EqualityMatchExpression("b", baseOperand["b"]));
    ElemMatchObjectMatchExpression op("a", eq.release());
    ASSERT(op.matchesSingleElement(match["a"]));
    ASSERT(!op.matchesSingleElement(notMatch["a"]));
}

TEST(ElemMatchObjectMatchExpression, MatchesElementArray) {
    BSONObj baseOperand = BSON("1" << 5);
    BSONObj match = BSON("a" << BSON_ARRAY(BSON_ARRAY('s' << 5.0)));
    BSONObj notMatch = BSON("a" << BSON_ARRAY(BSON_ARRAY(5 << 6)));
    unique_ptr<ComparisonMatchExpression> eq(new EqualityMatchExpression("1", baseOperand["1"]));
    ElemMatchObjectMatchExpression op("a", eq.release());
    ASSERT(op.matchesSingleElement(match["a"]));
    ASSERT(!op.matchesSingleElement(notMatch["a"]));
}

TEST(ElemMatchObjectMatchExpression, MatchesElementMultiple) {
    BSONObj baseOperand1 = BSON("b" << 5);
    BSONObj baseOperand2 = BSON("b" << 6);
    BSONObj baseOperand3 = BSON("c" << 7);
    BSONObj notMatch1 = BSON("a" << BSON_ARRAY(BSON("b" << 5 << "c" << 7)));
    BSONObj notMatch2 = BSON("a" << BSON_ARRAY(BSON("b" << 6 << "c" << 7)));
    BSONObj notMatch3 = BSON("a" << BSON_ARRAY(BSON("b" << BSON_ARRAY(5 << 6))));
    BSONObj match = BSON("a" << BSON_ARRAY(BSON("b" << BSON_ARRAY(5 << 6) << "c" << 7)));
    unique_ptr<ComparisonMatchExpression> eq1(new EqualityMatchExpression("b", baseOperand1["b"]));
    unique_ptr<ComparisonMatchExpression> eq2(new EqualityMatchExpression("b", baseOperand2["b"]));
    unique_ptr<ComparisonMatchExpression> eq3(new EqualityMatchExpression("c", baseOperand3["c"]));

    unique_ptr<AndMatchExpression> andOp(new AndMatchExpression());
    andOp->add(eq1.release());
    andOp->add(eq2.release());
    andOp->add(eq3.release());

    ElemMatchObjectMatchExpression op("a", andOp.release());
    ASSERT(!op.matchesSingleElement(notMatch1["a"]));
    ASSERT(!op.matchesSingleElement(notMatch2["a"]));
    ASSERT(!op.matchesSingleElement(notMatch3["a"]));
    ASSERT(op.matchesSingleElement(match["a"]));
}

TEST(ElemMatchObjectMatchExpression, MatchesNonArray) {
    BSONObj baseOperand = BSON("b" << 5);
    unique_ptr<ComparisonMatchExpression> eq(new EqualityMatchExpression("b", baseOperand["b"]));
    ElemMatchObjectMatchExpression op("a", eq.release());
    // Directly nested objects are not matched with $elemMatch.  An intervening array is
    // required.
    ASSERT(!op.matchesBSON(BSON("a" << BSON("b" << 5)), nullptr));
    ASSERT(!op.matchesBSON(BSON("a" << BSON("0" << (BSON("b" << 5)))), nullptr));
    ASSERT(!op.matchesBSON(BSON("a" << 4), nullptr));
}

TEST(ElemMatchObjectMatchExpression, MatchesArrayObject) {
    BSONObj baseOperand = BSON("b" << 5);
    unique_ptr<ComparisonMatchExpression> eq(new EqualityMatchExpression("b", baseOperand["b"]));
    ElemMatchObjectMatchExpression op("a", eq.release());
    ASSERT(op.matchesBSON(BSON("a" << BSON_ARRAY(BSON("b" << 5))), nullptr));
    ASSERT(op.matchesBSON(BSON("a" << BSON_ARRAY(4 << BSON("b" << 5))), nullptr));
    ASSERT(op.matchesBSON(BSON("a" << BSON_ARRAY(BSONObj() << BSON("b" << 5))), nullptr));
    ASSERT(op.matchesBSON(BSON("a" << BSON_ARRAY(BSON("b" << 6) << BSON("b" << 5))), nullptr));
}

TEST(ElemMatchObjectMatchExpression, MatchesMultipleNamedValues) {
    BSONObj baseOperand = BSON("c" << 5);
    unique_ptr<ComparisonMatchExpression> eq(new EqualityMatchExpression("c", baseOperand["c"]));
    ElemMatchObjectMatchExpression op("a.b", eq.release());
    ASSERT(
        op.matchesBSON(BSON("a" << BSON_ARRAY(BSON("b" << BSON_ARRAY(BSON("c" << 5))))), nullptr));
    ASSERT(op.matchesBSON(BSON("a" << BSON_ARRAY(BSON("b" << BSON_ARRAY(BSON("c" << 1)))
                                                 << BSON("b" << BSON_ARRAY(BSON("c" << 5))))),
                          nullptr));
}

TEST(ElemMatchObjectMatchExpression, ElemMatchKey) {
    BSONObj baseOperand = BSON("c" << 6);
    unique_ptr<ComparisonMatchExpression> eq(new EqualityMatchExpression("c", baseOperand["c"]));
    ElemMatchObjectMatchExpression op("a.b", eq.release());
    MatchDetails details;
    details.requestElemMatchKey();
    ASSERT(!op.matchesBSON(BSONObj(), &details));
    ASSERT(!details.hasElemMatchKey());
    ASSERT(!op.matchesBSON(BSON("a" << BSON("b" << BSON_ARRAY(BSON("c" << 7)))), &details));
    ASSERT(!details.hasElemMatchKey());
    ASSERT(op.matchesBSON(BSON("a" << BSON("b" << BSON_ARRAY(3 << BSON("c" << 6)))), &details));
    ASSERT(details.hasElemMatchKey());
    // The entry within the $elemMatch array is reported.
    ASSERT_EQUALS("1", details.elemMatchKey());
    ASSERT(op.matchesBSON(
        BSON("a" << BSON_ARRAY(1 << 2 << BSON("b" << BSON_ARRAY(3 << 5 << BSON("c" << 6))))),
        &details));
    ASSERT(details.hasElemMatchKey());
    // The entry within a parent of the $elemMatch array is reported.
    ASSERT_EQUALS("2", details.elemMatchKey());
}

TEST(ElemMatchObjectMatchExpression, Collation) {
    BSONObj baseOperand = BSON("b"
                               << "string");
    BSONObj match = BSON("a" << BSON_ARRAY(BSON("b"
                                                << "string")));
    BSONObj notMatch = BSON("a" << BSON_ARRAY(BSON("b"
                                                   << "string2")));
    unique_ptr<ComparisonMatchExpression> eq(new EqualityMatchExpression("b", baseOperand["b"]));
    ElemMatchObjectMatchExpression op("a", eq.release());
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kAlwaysEqual);
    op.setCollator(&collator);
    ASSERT(op.matchesSingleElement(match["a"]));
    ASSERT(op.matchesSingleElement(notMatch["a"]));
}

/**
TEST( ElemMatchObjectMatchExpression, MatchesIndexKey ) {
    BSONObj baseOperand = BSON( "b" << 5 );
    unique_ptr<ComparisonMatchExpression> eq( new ComparisonMatchExpression() );
    ASSERT( eq->init( "b", baseOperand[ "b" ] ).isOK() );
    ElemMatchObjectMatchExpression op;
    ASSERT( op.init( "a", eq.release() ).isOK() );
    IndexSpec indexSpec( BSON( "a.b" << 1 ) );
    BSONObj indexKey = BSON( "" << "5" );
    ASSERT( MatchMatchExpression::PartialMatchResult_Unknown ==
            op.matchesIndexKey( indexKey, indexSpec ) );
}
*/

TEST(ElemMatchValueMatchExpression, MatchesElementSingle) {
    BSONObj baseOperand = BSON("$gt" << 5);
    BSONObj match = BSON("a" << BSON_ARRAY(6));
    BSONObj notMatch = BSON("a" << BSON_ARRAY(4));
    unique_ptr<ComparisonMatchExpression> gt(new GTMatchExpression("", baseOperand["$gt"]));
    ElemMatchValueMatchExpression op("a", gt.release());
    ASSERT(op.matchesSingleElement(match["a"]));
    ASSERT(!op.matchesSingleElement(notMatch["a"]));
}

TEST(ElemMatchValueMatchExpression, MatchesElementMultiple) {
    BSONObj baseOperand1 = BSON("$gt" << 1);
    BSONObj baseOperand2 = BSON("$lt" << 10);
    BSONObj notMatch1 = BSON("a" << BSON_ARRAY(0 << 1));
    BSONObj notMatch2 = BSON("a" << BSON_ARRAY(10 << 11));
    BSONObj match = BSON("a" << BSON_ARRAY(0 << 5 << 11));
    unique_ptr<ComparisonMatchExpression> gt(new GTMatchExpression("", baseOperand1["$gt"]));
    unique_ptr<ComparisonMatchExpression> lt(new LTMatchExpression("", baseOperand2["$lt"]));

    ElemMatchValueMatchExpression op("a");
    op.add(gt.release());
    op.add(lt.release());

    ASSERT(!op.matchesSingleElement(notMatch1["a"]));
    ASSERT(!op.matchesSingleElement(notMatch2["a"]));
    ASSERT(op.matchesSingleElement(match["a"]));
}

TEST(ElemMatchValueMatchExpression, MatchesNonArray) {
    BSONObj baseOperand = BSON("$gt" << 5);
    unique_ptr<ComparisonMatchExpression> gt(new GTMatchExpression("", baseOperand["$gt"]));
    ElemMatchObjectMatchExpression op("a", gt.release());
    // Directly nested objects are not matched with $elemMatch.  An intervening array is
    // required.
    ASSERT(!op.matchesBSON(BSON("a" << 6), nullptr));
    ASSERT(!op.matchesBSON(BSON("a" << BSON("0" << 6)), nullptr));
}

TEST(ElemMatchValueMatchExpression, MatchesArrayScalar) {
    BSONObj baseOperand = BSON("$gt" << 5);
    unique_ptr<ComparisonMatchExpression> gt(new GTMatchExpression("", baseOperand["$gt"]));
    ElemMatchValueMatchExpression op("a", gt.release());
    ASSERT(op.matchesBSON(BSON("a" << BSON_ARRAY(6)), nullptr));
    ASSERT(op.matchesBSON(BSON("a" << BSON_ARRAY(4 << 6)), nullptr));
    ASSERT(op.matchesBSON(BSON("a" << BSON_ARRAY(BSONObj() << 7)), nullptr));
}

TEST(ElemMatchValueMatchExpression, MatchesMultipleNamedValues) {
    BSONObj baseOperand = BSON("$gt" << 5);
    unique_ptr<ComparisonMatchExpression> gt(new GTMatchExpression("", baseOperand["$gt"]));
    ElemMatchValueMatchExpression op("a.b", gt.release());
    ASSERT(op.matchesBSON(BSON("a" << BSON_ARRAY(BSON("b" << BSON_ARRAY(6)))), nullptr));
    ASSERT(op.matchesBSON(
        BSON("a" << BSON_ARRAY(BSON("b" << BSON_ARRAY(4)) << BSON("b" << BSON_ARRAY(4 << 6)))),
        nullptr));
}

TEST(ElemMatchValueMatchExpression, ElemMatchKey) {
    BSONObj baseOperand = BSON("$gt" << 6);
    unique_ptr<ComparisonMatchExpression> gt(new GTMatchExpression("", baseOperand["$gt"]));
    ElemMatchValueMatchExpression op("a.b", gt.release());
    MatchDetails details;
    details.requestElemMatchKey();
    ASSERT(!op.matchesBSON(BSONObj(), &details));
    ASSERT(!details.hasElemMatchKey());
    ASSERT(!op.matchesBSON(BSON("a" << BSON("b" << BSON_ARRAY(2))), &details));
    ASSERT(!details.hasElemMatchKey());
    ASSERT(op.matchesBSON(BSON("a" << BSON("b" << BSON_ARRAY(3 << 7))), &details));
    ASSERT(details.hasElemMatchKey());
    // The entry within the $elemMatch array is reported.
    ASSERT_EQUALS("1", details.elemMatchKey());
    ASSERT(op.matchesBSON(BSON("a" << BSON_ARRAY(1 << 2 << BSON("b" << BSON_ARRAY(3 << 7)))),
                          &details));
    ASSERT(details.hasElemMatchKey());
    // The entry within a parent of the $elemMatch array is reported.
    ASSERT_EQUALS("2", details.elemMatchKey());
}

/**
TEST( ElemMatchValueMatchExpression, MatchesIndexKey ) {
    BSONObj baseOperand = BSON( "$lt" << 5 );
    unique_ptr<LtOp> lt( new ComparisonMatchExpression() );
    ASSERT( lt->init( "a", baseOperand[ "$lt" ] ).isOK() );
    ElemMatchValueMatchExpression op;
    ASSERT( op.init( "a", lt.release() ).isOK() );
    IndexSpec indexSpec( BSON( "a" << 1 ) );
    BSONObj indexKey = BSON( "" << "3" );
    ASSERT( MatchMatchExpression::PartialMatchResult_Unknown ==
            op.matchesIndexKey( indexKey, indexSpec ) );
}
*/

TEST(AndOfElemMatch, MatchesElement) {
    BSONObj baseOperanda1 = BSON("a" << 1);
    unique_ptr<ComparisonMatchExpression> eqa1(
        new EqualityMatchExpression("a", baseOperanda1["a"]));

    BSONObj baseOperandb1 = BSON("b" << 1);
    unique_ptr<ComparisonMatchExpression> eqb1(
        new EqualityMatchExpression("b", baseOperandb1["b"]));

    unique_ptr<AndMatchExpression> and1(new AndMatchExpression());
    and1->add(eqa1.release());
    and1->add(eqb1.release());
    // and1 = { a : 1, b : 1 }

    unique_ptr<ElemMatchObjectMatchExpression> elemMatch1(
        new ElemMatchObjectMatchExpression("x", and1.release()));
    // elemMatch1 = { x : { $elemMatch : { a : 1, b : 1 } } }

    BSONObj baseOperanda2 = BSON("a" << 2);
    unique_ptr<ComparisonMatchExpression> eqa2(
        new EqualityMatchExpression("a", baseOperanda2["a"]));

    BSONObj baseOperandb2 = BSON("b" << 2);
    unique_ptr<ComparisonMatchExpression> eqb2(
        new EqualityMatchExpression("b", baseOperandb2["b"]));

    unique_ptr<AndMatchExpression> and2(new AndMatchExpression());
    and2->add(eqa2.release());
    and2->add(eqb2.release());
    // and2 = { a : 2, b : 2 }

    unique_ptr<ElemMatchObjectMatchExpression> elemMatch2(
        new ElemMatchObjectMatchExpression("x", and2.release()));
    // elemMatch2 = { x : { $elemMatch : { a : 2, b : 2 } } }

    unique_ptr<AndMatchExpression> andOfEM(new AndMatchExpression());
    andOfEM->add(elemMatch1.release());
    andOfEM->add(elemMatch2.release());

    BSONObj nonArray = BSON("x" << 4);
    ASSERT(!andOfEM->matchesSingleElement(nonArray["x"]));
    BSONObj emptyArray = BSON("x" << BSONArray());
    ASSERT(!andOfEM->matchesSingleElement(emptyArray["x"]));
    BSONObj nonObjArray = BSON("x" << BSON_ARRAY(4));
    ASSERT(!andOfEM->matchesSingleElement(nonObjArray["x"]));
    BSONObj singleObjMatch = BSON("x" << BSON_ARRAY(BSON("a" << 1 << "b" << 1)));
    ASSERT(!andOfEM->matchesSingleElement(singleObjMatch["x"]));
    BSONObj otherObjMatch = BSON("x" << BSON_ARRAY(BSON("a" << 2 << "b" << 2)));
    ASSERT(!andOfEM->matchesSingleElement(otherObjMatch["x"]));
    BSONObj bothObjMatch =
        BSON("x" << BSON_ARRAY(BSON("a" << 1 << "b" << 1) << BSON("a" << 2 << "b" << 2)));
    ASSERT(andOfEM->matchesSingleElement(bothObjMatch["x"]));
    BSONObj noObjMatch =
        BSON("x" << BSON_ARRAY(BSON("a" << 1 << "b" << 2) << BSON("a" << 2 << "b" << 1)));
    ASSERT(!andOfEM->matchesSingleElement(noObjMatch["x"]));
}

TEST(AndOfElemMatch, Matches) {
    BSONObj baseOperandgt1 = BSON("$gt" << 1);
    unique_ptr<ComparisonMatchExpression> gt1(new GTMatchExpression("", baseOperandgt1["$gt"]));

    BSONObj baseOperandlt1 = BSON("$lt" << 10);
    unique_ptr<ComparisonMatchExpression> lt1(new LTMatchExpression("", baseOperandlt1["$lt"]));

    unique_ptr<ElemMatchValueMatchExpression> elemMatch1(new ElemMatchValueMatchExpression("x"));
    elemMatch1->add(gt1.release());
    elemMatch1->add(lt1.release());
    // elemMatch1 = { x : { $elemMatch : { $gt : 1 , $lt : 10 } } }

    BSONObj baseOperandgt2 = BSON("$gt" << 101);
    unique_ptr<ComparisonMatchExpression> gt2(new GTMatchExpression("", baseOperandgt2["$gt"]));

    BSONObj baseOperandlt2 = BSON("$lt" << 110);
    unique_ptr<ComparisonMatchExpression> lt2(new LTMatchExpression("", baseOperandlt2["$lt"]));

    unique_ptr<ElemMatchValueMatchExpression> elemMatch2(new ElemMatchValueMatchExpression("x"));
    elemMatch2->add(gt2.release());
    elemMatch2->add(lt2.release());
    // elemMatch2 = { x : { $elemMatch : { $gt : 101 , $lt : 110 } } }

    unique_ptr<AndMatchExpression> andOfEM(new AndMatchExpression());
    andOfEM->add(elemMatch1.release());
    andOfEM->add(elemMatch2.release());

    BSONObj nonArray = BSON("x" << 4);
    ASSERT(!andOfEM->matchesBSON(nonArray, nullptr));
    BSONObj emptyArray = BSON("x" << BSONArray());
    ASSERT(!andOfEM->matchesBSON(emptyArray, nullptr));
    BSONObj nonNumberArray = BSON("x" << BSON_ARRAY("q"));
    ASSERT(!andOfEM->matchesBSON(nonNumberArray, nullptr));
    BSONObj singleMatch = BSON("x" << BSON_ARRAY(5));
    ASSERT(!andOfEM->matchesBSON(singleMatch, nullptr));
    BSONObj otherMatch = BSON("x" << BSON_ARRAY(105));
    ASSERT(!andOfEM->matchesBSON(otherMatch, nullptr));
    BSONObj bothMatch = BSON("x" << BSON_ARRAY(5 << 105));
    ASSERT(andOfEM->matchesBSON(bothMatch, nullptr));
    BSONObj neitherMatch = BSON("x" << BSON_ARRAY(0 << 200));
    ASSERT(!andOfEM->matchesBSON(neitherMatch, nullptr));
}

TEST(SizeMatchExpression, MatchesElement) {
    BSONObj match = BSON("a" << BSON_ARRAY(5 << 6));
    BSONObj notMatch = BSON("a" << BSON_ARRAY(5));
    SizeMatchExpression size("", 2);
    ASSERT(size.matchesSingleElement(match.firstElement()));
    ASSERT(!size.matchesSingleElement(notMatch.firstElement()));
}

TEST(SizeMatchExpression, MatchesNonArray) {
    // Non arrays do not match.
    BSONObj stringValue = BSON("a"
                               << "z");
    BSONObj numberValue = BSON("a" << 0);
    BSONObj arrayValue = BSON("a" << BSONArray());
    SizeMatchExpression size("", 0);
    ASSERT(!size.matchesSingleElement(stringValue.firstElement()));
    ASSERT(!size.matchesSingleElement(numberValue.firstElement()));
    ASSERT(size.matchesSingleElement(arrayValue.firstElement()));
}

TEST(SizeMatchExpression, MatchesArray) {
    SizeMatchExpression size("a", 2);
    ASSERT(size.matchesBSON(BSON("a" << BSON_ARRAY(4 << 5.5)), nullptr));
    // Arrays are not unwound to look for matching subarrays.
    ASSERT(!size.matchesBSON(BSON("a" << BSON_ARRAY(4 << 5.5 << BSON_ARRAY(1 << 2))), nullptr));
}

TEST(SizeMatchExpression, MatchesNestedArray) {
    SizeMatchExpression size("a.2", 2);
    // A numerically referenced nested array is matched.
    ASSERT(size.matchesBSON(BSON("a" << BSON_ARRAY(4 << 5.5 << BSON_ARRAY(1 << 2))), nullptr));
}

TEST(SizeMatchExpression, ElemMatchKey) {
    SizeMatchExpression size("a.b", 3);
    MatchDetails details;
    details.requestElemMatchKey();
    ASSERT(!size.matchesBSON(BSON("a" << 1), &details));
    ASSERT(!details.hasElemMatchKey());
    ASSERT(size.matchesBSON(BSON("a" << BSON("b" << BSON_ARRAY(1 << 2 << 3))), &details));
    ASSERT(!details.hasElemMatchKey());
    ASSERT(size.matchesBSON(BSON("a" << BSON_ARRAY(2 << BSON("b" << BSON_ARRAY(1 << 2 << 3)))),
                            &details));
    ASSERT(details.hasElemMatchKey());
    ASSERT_EQUALS("1", details.elemMatchKey());
}

TEST(SizeMatchExpression, Equivalent) {
    SizeMatchExpression e1("a", 5);
    SizeMatchExpression e2("a", 6);
    SizeMatchExpression e3("v", 5);

    ASSERT(e1.equivalent(&e1));
    ASSERT(!e1.equivalent(&e2));
    ASSERT(!e1.equivalent(&e3));
}

/**
   TEST( SizeMatchExpression, MatchesIndexKey ) {
   BSONObj operand = BSON( "$size" << 4 );
   SizeMatchExpression size;
   ASSERT( size.init( "a", operand[ "$size" ] ).isOK() );
   IndexSpec indexSpec( BSON( "a" << 1 ) );
   BSONObj indexKey = BSON( "" << 1 );
   ASSERT( MatchMatchExpression::PartialMatchResult_Unknown ==
   size.matchesIndexKey( indexKey, indexSpec ) );
   }
*/

}  // namespace monger
