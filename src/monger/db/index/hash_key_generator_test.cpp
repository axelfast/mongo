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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kIndex

#include "monger/platform/basic.h"

#include "monger/db/index/expression_keys_private.h"

#include <algorithm>

#include "monger/bson/bsonobjbuilder.h"
#include "monger/bson/simple_bsonobj_comparator.h"
#include "monger/db/hasher.h"
#include "monger/db/json.h"
#include "monger/db/query/collation/collator_interface_mock.h"
#include "monger/unittest/unittest.h"
#include "monger/util/log.h"

using namespace monger;

namespace {

const HashSeed kHashSeed = 0;
const int kHashVersion = 0;

std::string dumpKeyset(const BSONObjSet& objs) {
    std::stringstream ss;
    ss << "[ ";
    for (BSONObjSet::iterator i = objs.begin(); i != objs.end(); ++i) {
        ss << i->toString() << " ";
    }
    ss << "]";

    return ss.str();
}

bool assertKeysetsEqual(const BSONObjSet& expectedKeys, const BSONObjSet& actualKeys) {
    if (expectedKeys.size() != actualKeys.size()) {
        log() << "Expected: " << dumpKeyset(expectedKeys) << ", "
              << "Actual: " << dumpKeyset(actualKeys);
        return false;
    }

    if (!std::equal(expectedKeys.begin(),
                    expectedKeys.end(),
                    actualKeys.begin(),
                    SimpleBSONObjComparator::kInstance.makeEqualTo())) {
        log() << "Expected: " << dumpKeyset(expectedKeys) << ", "
              << "Actual: " << dumpKeyset(actualKeys);
        return false;
    }

    return true;
}

BSONObj makeHashKey(BSONElement elt) {
    return BSON("" << BSONElementHasher::hash64(elt, kHashSeed));
}

TEST(HashKeyGeneratorTest, CollationAppliedBeforeHashing) {
    BSONObj obj = fromjson("{a: 'string'}");
    BSONObjSet actualKeys = SimpleBSONObjComparator::kInstance.makeBSONObjSet();
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);
    ExpressionKeysPrivate::getHashKeys(
        obj, "a", kHashSeed, kHashVersion, false, &collator, &actualKeys);

    BSONObj backwardsObj = fromjson("{a: 'gnirts'}");
    BSONObjSet expectedKeys = SimpleBSONObjComparator::kInstance.makeBSONObjSet();
    expectedKeys.insert(makeHashKey(backwardsObj["a"]));

    ASSERT(assertKeysetsEqual(expectedKeys, actualKeys));
}

TEST(HashKeyGeneratorTest, CollationDoesNotAffectNonStringFields) {
    BSONObj obj = fromjson("{a: 5}");
    BSONObjSet actualKeys = SimpleBSONObjComparator::kInstance.makeBSONObjSet();
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);
    ExpressionKeysPrivate::getHashKeys(
        obj, "a", kHashSeed, kHashVersion, false, &collator, &actualKeys);

    BSONObjSet expectedKeys = SimpleBSONObjComparator::kInstance.makeBSONObjSet();
    expectedKeys.insert(makeHashKey(obj["a"]));

    ASSERT(assertKeysetsEqual(expectedKeys, actualKeys));
}

TEST(HashKeyGeneratorTest, CollatorAppliedBeforeHashingNestedObject) {
    BSONObj obj = fromjson("{a: {b: 'string'}}");
    BSONObj backwardsObj = fromjson("{a: {b: 'gnirts'}}");
    BSONObjSet actualKeys = SimpleBSONObjComparator::kInstance.makeBSONObjSet();
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);
    ExpressionKeysPrivate::getHashKeys(
        obj, "a", kHashSeed, kHashVersion, false, &collator, &actualKeys);

    BSONObjSet expectedKeys = SimpleBSONObjComparator::kInstance.makeBSONObjSet();
    expectedKeys.insert(makeHashKey(backwardsObj["a"]));

    ASSERT(assertKeysetsEqual(expectedKeys, actualKeys));
}

TEST(HashKeyGeneratorTest, NoCollation) {
    BSONObj obj = fromjson("{a: 'string'}");
    BSONObjSet actualKeys = SimpleBSONObjComparator::kInstance.makeBSONObjSet();
    ExpressionKeysPrivate::getHashKeys(
        obj, "a", kHashSeed, kHashVersion, false, nullptr, &actualKeys);

    BSONObjSet expectedKeys = SimpleBSONObjComparator::kInstance.makeBSONObjSet();
    expectedKeys.insert(makeHashKey(obj["a"]));

    ASSERT(assertKeysetsEqual(expectedKeys, actualKeys));
}

}  // namespace
