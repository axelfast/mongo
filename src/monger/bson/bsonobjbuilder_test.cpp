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
#include "monger/unittest/unittest.h"

namespace monger {
namespace {

using std::string;

const long long maxEncodableInt = (1 << 30) - 1;
const long long minEncodableInt = -maxEncodableInt;

const long long maxInt = (std::numeric_limits<int>::max)();
const long long minInt = (std::numeric_limits<int>::min)();

const long long maxEncodableDouble = (1LL << 40) - 1;
const long long minEncodableDouble = -maxEncodableDouble;

const long long maxDouble = (1LL << std::numeric_limits<double>::digits) - 1;
const long long minDouble = -maxDouble;

const long long maxLongLong = (std::numeric_limits<long long>::max)();
const long long minLongLong = (std::numeric_limits<long long>::min)();

template <typename T>
void assertBSONTypeEquals(BSONType actual, BSONType expected, T value, int i) {
    if (expected != actual) {
        std::stringstream ss;
        ss << "incorrect type in bson object for " << (i + 1) << "-th test value " << value
           << ". actual: " << monger::typeName(actual)
           << "; expected: " << monger::typeName(expected);
        const string msg = ss.str();
        FAIL(msg);
    }
}

TEST(BSONObjBuilderTest, AppendInt64T) {
    auto obj = BSON("a" << int64_t{5} << "b" << int64_t{1ll << 40});
    ASSERT_EQ(obj["a"].type(), NumberLong);
    ASSERT_EQ(obj["b"].type(), NumberLong);
    ASSERT_EQ(obj["a"].Long(), 5);
    ASSERT_EQ(obj["b"].Long(), 1ll << 40);
}

/**
 * current conversion ranges in append(unsigned n)
 * dbl/int max/min in comments refer to max/min encodable constants
 *                  0 <= n <= uint_max          -----> int
 */

TEST(BSONObjBuilderTest, AppendUnsignedInt) {
    struct {
        unsigned int v;
        BSONType t;
    } data[] = {{0, monger::NumberInt},
                {100, monger::NumberInt},
                {maxEncodableInt, monger::NumberInt},
                {maxEncodableInt + 1, monger::NumberInt},
                {static_cast<unsigned int>(maxInt), monger::NumberInt},
                {static_cast<unsigned int>(maxInt) + 1U, monger::NumberInt},
                {(std::numeric_limits<unsigned int>::max)(), monger::NumberInt},
                {0, monger::Undefined}};
    for (int i = 0; data[i].t != monger::Undefined; i++) {
        unsigned int v = data[i].v;
        BSONObjBuilder b;
        b.append("a", v);
        BSONObj o = b.obj();
        ASSERT_EQUALS(o.nFields(), 1);
        BSONElement e = o.getField("a");
        unsigned int n = e.numberLong();
        ASSERT_EQUALS(n, v);
        assertBSONTypeEquals(e.type(), data[i].t, v, i);
    }
}

/**
 * current conversion ranges in appendIntOrLL(long long n)
 * dbl/int max/min in comments refer to max/min encodable constants
 *                       n <  dbl_min            -----> long long
 *            dbl_min <= n <  int_min            -----> double
 *            int_min <= n <= int_max            -----> int
 *            int_max <  n <= dbl_max            -----> double
 *            dbl_max <  n                       -----> long long
 */

TEST(BSONObjBuilderTest, AppendIntOrLL) {
    struct {
        long long v;
        BSONType t;
    } data[] = {{0, monger::NumberInt},
                {-100, monger::NumberInt},
                {100, monger::NumberInt},
                {-(maxInt / 2 - 1), monger::NumberInt},
                {maxInt / 2 - 1, monger::NumberInt},
                {-(maxInt / 2), monger::NumberLong},
                {maxInt / 2, monger::NumberLong},
                {minEncodableInt, monger::NumberLong},
                {maxEncodableInt, monger::NumberLong},
                {minEncodableInt - 1, monger::NumberLong},
                {maxEncodableInt + 1, monger::NumberLong},
                {minInt, monger::NumberLong},
                {maxInt, monger::NumberLong},
                {minInt - 1, monger::NumberLong},
                {maxInt + 1, monger::NumberLong},
                {minLongLong, monger::NumberLong},
                {maxLongLong, monger::NumberLong},
                {0, monger::Undefined}};
    for (int i = 0; data[i].t != monger::Undefined; i++) {
        long long v = data[i].v;
        BSONObjBuilder b;
        b.appendIntOrLL("a", v);
        BSONObj o = b.obj();
        ASSERT_EQUALS(o.nFields(), 1);
        BSONElement e = o.getField("a");
        long long n = e.numberLong();
        ASSERT_EQUALS(n, v);
        assertBSONTypeEquals(e.type(), data[i].t, v, i);
    }
}

/**
 * current conversion ranges in appendNumber(size_t n)
 * dbl/int max/min in comments refer to max/min encodable constants
 *                  0 <= n <= int_max            -----> int
 *            int_max <  n                       -----> long long
 */

TEST(BSONObjBuilderTest, AppendNumberSizeT) {
    struct {
        size_t v;
        BSONType t;
    } data[] = {{0, monger::NumberInt},
                {100, monger::NumberInt},
                {maxEncodableInt, monger::NumberInt},
                {maxEncodableInt + 1, monger::NumberLong},
                {size_t(maxInt), monger::NumberLong},
                {size_t(maxInt) + 1U, monger::NumberLong},
                {(std::numeric_limits<size_t>::max)(), monger::NumberLong},
                {0, monger::Undefined}};
    for (int i = 0; data[i].t != monger::Undefined; i++) {
        size_t v = data[i].v;
        BSONObjBuilder b;
        b.appendNumber("a", v);
        BSONObj o = b.obj();
        ASSERT_EQUALS(o.nFields(), 1);
        BSONElement e = o.getField("a");
        size_t n = e.numberLong();
        ASSERT_EQUALS(n, v);
        assertBSONTypeEquals(e.type(), data[i].t, v, i);
    }
}

/**
 * current conversion ranges in appendNumber(long long n)
 * dbl/int max/min in comments refer to max/min encodable constants
 *                       n <  dbl_min            -----> long long
 *            dbl_min <= n <  int_min            -----> double
 *            int_min <= n <= int_max            -----> int
 *            int_max <  n <= dbl_max            -----> double
 *            dbl_max <  n                       -----> long long
 */

TEST(BSONObjBuilderTest, AppendNumberLongLong) {
    struct {
        long long v;
        BSONType t;
    } data[] = {{0, monger::NumberInt},
                {-100, monger::NumberInt},
                {100, monger::NumberInt},
                {minEncodableInt, monger::NumberInt},
                {maxEncodableInt, monger::NumberInt},
                {minEncodableInt - 1, monger::NumberDouble},
                {maxEncodableInt + 1, monger::NumberDouble},
                {minInt, monger::NumberDouble},
                {maxInt, monger::NumberDouble},
                {minInt - 1, monger::NumberDouble},
                {maxInt + 1, monger::NumberDouble},
                {minEncodableDouble, monger::NumberDouble},
                {maxEncodableDouble, monger::NumberDouble},
                {minEncodableDouble - 1, monger::NumberLong},
                {maxEncodableDouble + 1, monger::NumberLong},
                {minDouble, monger::NumberLong},
                {maxDouble, monger::NumberLong},
                {minDouble - 1, monger::NumberLong},
                {maxDouble + 1, monger::NumberLong},
                {minLongLong, monger::NumberLong},
                {maxLongLong, monger::NumberLong},
                {0, monger::Undefined}};
    for (int i = 0; data[i].t != monger::Undefined; i++) {
        long long v = data[i].v;
        BSONObjBuilder b;
        b.appendNumber("a", v);
        BSONObj o = b.obj();
        ASSERT_EQUALS(o.nFields(), 1);
        BSONElement e = o.getField("a");
        if (data[i].t != monger::NumberDouble) {
            long long n = e.numberLong();
            ASSERT_EQUALS(n, v);
        } else {
            double n = e.numberDouble();
            ASSERT_APPROX_EQUAL(n, static_cast<double>(v), 0.001);
        }
        assertBSONTypeEquals(e.type(), data[i].t, v, i);
    }
}

TEST(BSONObjBuilderTest, StreamLongLongMin) {
    BSONObj o = BSON("a" << std::numeric_limits<long long>::min());
    ASSERT_EQUALS(o.nFields(), 1);
    BSONElement e = o.getField("a");
    long long n = e.numberLong();
    ASSERT_EQUALS(n, std::numeric_limits<long long>::min());
}

TEST(BSONObjBuilderTest, AppendNumberLongLongMinCompareObject) {
    BSONObjBuilder b;
    b.appendNumber("a", std::numeric_limits<long long>::min());
    BSONObj o1 = b.obj();

    BSONObj o2 = BSON("a" << std::numeric_limits<long long>::min());

    ASSERT_BSONOBJ_EQ(o1, o2);
}

TEST(BSONObjBuilderTest, AppendMaxTimestampConversion) {
    BSONObjBuilder b;
    b.appendMaxForType("a", monger::bsonTimestamp);
    BSONObj o1 = b.obj();

    BSONElement e = o1.getField("a");
    ASSERT_FALSE(e.eoo());

    monger::Timestamp timestamp = e.timestamp();
    ASSERT_FALSE(timestamp.isNull());
}

TEST(BSONObjBuilderTest, ResumeBuilding) {
    BufBuilder b;
    {
        BSONObjBuilder firstBuilder(b);
        firstBuilder.append("a", "b");
    }
    {
        BSONObjBuilder secondBuilder(BSONObjBuilder::ResumeBuildingTag(), b);
        secondBuilder.append("c", "d");
    }
    auto obj = BSONObj(b.buf());
    ASSERT_BSONOBJ_EQ(obj,
                      BSON("a"
                           << "b"
                           << "c"
                           << "d"));
}

TEST(BSONObjBuilderTest, ResumeBuildingWithNesting) {
    BufBuilder b;
    // build a trivial object.
    {
        BSONObjBuilder firstBuilder(b);
        firstBuilder.append("ll",
                            BSON("f" << BSON("cc"
                                             << "dd")));
    }
    // add a complex field
    {
        BSONObjBuilder secondBuilder(BSONObjBuilder::ResumeBuildingTag(), b);
        secondBuilder.append("a", BSON("c" << 3));
    }
    auto obj = BSONObj(b.buf());
    ASSERT_BSONOBJ_EQ(obj,
                      BSON("ll" << BSON("f" << BSON("cc"
                                                    << "dd"))
                                << "a"
                                << BSON("c" << 3)));
}

TEST(BSONObjBuilderTest, ResetToEmptyResultsInEmptyObj) {
    BSONObjBuilder bob;
    bob.append("a", 3);
    bob.resetToEmpty();
    ASSERT_BSONOBJ_EQ(BSONObj(), bob.obj());
}

TEST(BSONObjBuilderTest, ResetToEmptyForNestedBuilderOnlyResetsInnerObj) {
    BSONObjBuilder bob;
    bob.append("a", 3);
    BSONObjBuilder innerObj(bob.subobjStart("nestedObj"));
    innerObj.append("b", 4);
    innerObj.resetToEmpty();
    innerObj.done();
    ASSERT_BSONOBJ_EQ(BSON("a" << 3 << "nestedObj" << BSONObj()), bob.obj());
}

TEST(BSONObjBuilderTest, MovingAnOwningBSONObjBuilderWorks) {
    BSONObjBuilder initial;
    initial.append("a", 1);

    BSONObjBuilder bob(std::move(initial));
    ASSERT(bob.owned());
    bob.append("b", 2);
    bob << "c" << 3;
    ASSERT_BSONOBJ_EQ(bob.obj(), BSON("a" << 1 << "b" << 2 << "c" << 3));
}

TEST(BSONObjBuilderTest, MovingANonOwningBSONObjBuilderWorks) {
    BSONObjBuilder outer;
    {
        BSONObjBuilder initial(outer.subobjStart("nested"));
        initial.append("a", 1);

        BSONObjBuilder bob(std::move(initial));
        ASSERT(!bob.owned());
        ASSERT_EQ(&bob.bb(), &outer.bb());

        bob.append("b", 2);
        bob << "c" << 3;
    }

    ASSERT_BSONOBJ_EQ(outer.obj(), BSON("nested" << BSON("a" << 1 << "b" << 2 << "c" << 3)));
}

TEST(BSONArrayBuilderTest, MovingABSONArrayBuilderWorks) {
    BSONObjBuilder bob;
    bob.append("a", 1);

    BSONArrayBuilder initial(bob.subarrayStart("array"));
    initial.append(1);
    initial << "2";

    BSONArrayBuilder moved(std::move(initial));
    moved.append(3);
    moved << "4";

    moved.done();

    ASSERT_BSONOBJ_EQ(bob.obj(), BSON("a" << 1 << "array" << BSON_ARRAY(1 << "2" << 3 << "4")));
}

TEST(BSONObjBuilderTest, SeedingBSONObjBuilderWithRootedUnsharedOwnedBsonWorks) {
    auto origObj = BSON("a" << 1);
    auto origObjPtr = origObj.objdata();

    BSONObjBuilder bob(std::move(origObj));  // moving.
    bob.append("b", 1);
    const auto obj = bob.obj();
    ASSERT_BSONOBJ_EQ(origObj, BSONObj());
    ASSERT_BSONOBJ_EQ(obj, BSON("a" << 1 << "b" << 1));
    ASSERT_EQ(static_cast<const void*>(obj.objdata()), static_cast<const void*>(origObjPtr));
}

TEST(BSONObjBuilderTest, SeedingBSONObjBuilderWithRootedSharedOwnedBsonWorks) {
    const auto origObj = BSON("a" << 1);
    auto origObjPtr = origObj.objdata();

    BSONObjBuilder bob(origObj);  // not moving.
    bob.append("b", 1);
    const auto obj = bob.obj();
    ASSERT_BSONOBJ_EQ(origObj, BSON("a" << 1));
    ASSERT_BSONOBJ_EQ(obj, BSON("a" << 1 << "b" << 1));
    ASSERT_NE(static_cast<const void*>(obj.objdata()), static_cast<const void*>(origObjPtr));
}

TEST(BSONObjBuilderTest, SeedingBSONObjBuilderWithRootedUnownedBsonWorks) {
    const auto origObjHolder = BSON("a" << 1);
    auto origObjPtr = origObjHolder.objdata();
    const auto origObj = BSONObj(origObjPtr);
    invariant(!origObj.isOwned());

    BSONObjBuilder bob(origObj);  // not moving.
    bob.append("b", 1);
    const auto obj = bob.obj();
    ASSERT_BSONOBJ_EQ(origObj, BSON("a" << 1));
    ASSERT_BSONOBJ_EQ(obj, BSON("a" << 1 << "b" << 1));
    ASSERT_NE(static_cast<const void*>(obj.objdata()), static_cast<const void*>(origObjPtr));
}

TEST(BSONObjBuilderTest, SeedingBSONObjBuilderWithNonrootedUnsharedOwnedBsonWorks) {
    auto origObjHolder = BSON("" << BSON("a" << 1));
    auto origObj = origObjHolder.firstElement().Obj();
    auto origObjPtr = origObj.objdata();
    origObj.shareOwnershipWith(origObjHolder.releaseSharedBuffer());

    BSONObjBuilder bob(std::move(origObj));  // moving.
    bob.append("b", 1);
    const auto obj = bob.obj();
    ASSERT_BSONOBJ_EQ(origObj, BSONObj());
    ASSERT_BSONOBJ_EQ(obj, BSON("a" << 1 << "b" << 1));
    ASSERT_EQ(static_cast<const void*>(obj.objdata()), static_cast<const void*>(origObjPtr));
}

TEST(BSONObjBuilderTest, SeedingBSONObjBuilderWithNonrootedSharedOwnedBsonWorks) {
    auto origObjHolder = BSON("" << BSON("a" << 1));
    auto origObj = origObjHolder.firstElement().Obj();
    auto origObjPtr = origObj.objdata();
    origObj.shareOwnershipWith(origObjHolder.releaseSharedBuffer());

    BSONObjBuilder bob(origObj);  // not moving.
    bob.append("b", 1);
    const auto obj = bob.obj();
    ASSERT_BSONOBJ_EQ(origObj, BSON("a" << 1));
    ASSERT_BSONOBJ_EQ(obj, BSON("a" << 1 << "b" << 1));
    ASSERT_NE(static_cast<const void*>(obj.objdata()), static_cast<const void*>(origObjPtr));
}

TEST(BSONObjBuilderTest, SeedingBSONObjBuilderWithNonrootedUnownedBsonWorks) {
    auto origObjHolder = BSON("" << BSON("a" << 1));
    auto origObj = origObjHolder.firstElement().Obj();
    auto origObjPtr = origObj.objdata();
    invariant(!origObj.isOwned());

    BSONObjBuilder bob(origObj);  // not moving.
    bob.append("b", 1);
    const auto obj = bob.obj();
    ASSERT_BSONOBJ_EQ(origObj, BSON("a" << 1));
    ASSERT_BSONOBJ_EQ(obj, BSON("a" << 1 << "b" << 1));
    ASSERT_NE(static_cast<const void*>(obj.objdata()), static_cast<const void*>(origObjPtr));
}

TEST(BSONObjBuilderTest, SizeChecks) {
    auto generateBuffer = [](std::int32_t size) {
        std::vector<char> buffer(size);
        DataRange bufferRange(&buffer.front(), &buffer.back());
        ASSERT_OK(bufferRange.writeNoThrow(LittleEndian<int32_t>(size)));

        return buffer;
    };

    {
        // Implicitly assert that BSONObjBuilder does not throw
        // with standard size buffers.
        auto normalBuffer = generateBuffer(15 * 1024 * 1024);
        BSONObj obj(normalBuffer.data());

        BSONObjBuilder builder;
        builder.append("a", obj);
        BSONObj finalObj = builder.obj();
    }

    // Large buffers cause an exception to be thrown.
    ASSERT_THROWS_CODE(
        [&] {
            auto largeBuffer = generateBuffer(17 * 1024 * 1024);
            BSONObj obj(largeBuffer.data(), BSONObj::LargeSizeTrait{});

            BSONObjBuilder builder;
            builder.append("a", obj);
            BSONObj finalObj = builder.obj();
        }(),
        DBException,
        ErrorCodes::BSONObjectTooLarge);


    // Assert that the max size can be increased by passing BSONObj a tag type.
    {
        auto largeBuffer = generateBuffer(17 * 1024 * 1024);
        BSONObj obj(largeBuffer.data(), BSONObj::LargeSizeTrait{});

        BSONObjBuilder builder;
        builder.append("a", obj);
        BSONObj finalObj = builder.obj<BSONObj::LargeSizeTrait>();
    }

    // But a size is in fact being enforced.
    {
        auto largeBuffer = generateBuffer(40 * 1024 * 1024);
        BSONObj obj(largeBuffer.data(), BSONObj::LargeSizeTrait{});
        BSONObjBuilder builder;
        ASSERT_THROWS(
            [&]() {

                for (StringData character : {"a", "b", "c"}) {
                    builder.append(character, obj);
                }
                BSONObj finalObj = builder.obj<BSONObj::LargeSizeTrait>();

            }(),
            DBException);
    }
}


}  // namespace
}  // namespace monger
