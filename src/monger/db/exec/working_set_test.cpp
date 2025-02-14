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

/**
 * This file contains tests for monger/db/exec/working_set.cpp
 */


#include "monger/db/exec/working_set.h"
#include "monger/db/jsobj.h"
#include "monger/db/json.h"
#include "monger/db/storage/snapshot.h"
#include "monger/unittest/unittest.h"
#include "monger/util/assert_util.h"

using namespace monger;

namespace {

using std::string;

class WorkingSetFixture : public monger::unittest::Test {
protected:
    void setUp() {
        ws.reset(new WorkingSet());
        id = ws->allocate();
        ASSERT(id != WorkingSet::INVALID_ID);
        member = ws->get(id);
        ASSERT(nullptr != member);
    }

    void tearDown() {
        ws.reset();
        member = nullptr;
    }

    std::unique_ptr<WorkingSet> ws;
    WorkingSetID id;
    WorkingSetMember* member;
};

TEST_F(WorkingSetFixture, noFieldToGet) {
    BSONElement elt;

    // Make sure we're not getting anything out of an invalid WSM.
    ASSERT_EQUALS(WorkingSetMember::INVALID, member->getState());
    ASSERT_FALSE(member->getFieldDotted("foo", &elt));

    ws->transitionToRecordIdAndIdx(id);
    ASSERT_FALSE(member->getFieldDotted("foo", &elt));

    // Our state is that of a valid object.  The getFieldDotted shouldn't throw; there's
    // something to call getFieldDotted on, but there's no field there.
    ws->transitionToRecordIdAndObj(id);
    ASSERT_TRUE(member->getFieldDotted("foo", &elt));

    WorkingSetMember* member = ws->get(id);
    member->obj = {SnapshotId(),
                   BSON("fake"
                        << "obj")};
    ws->transitionToOwnedObj(id);
    ASSERT_TRUE(member->getFieldDotted("foo", &elt));
}

TEST_F(WorkingSetFixture, getFieldUnowned) {
    string fieldName = "x";

    BSONObj obj = BSON(fieldName << 5);
    // Not truthful since the RecordId is bogus, but the RecordId isn't accessed anyway...
    ws->transitionToRecordIdAndObj(id);
    member->obj = Snapshotted<BSONObj>(SnapshotId(), BSONObj(obj.objdata()));
    ASSERT_TRUE(obj.isOwned());
    ASSERT_FALSE(member->obj.value().isOwned());

    // Get out the field we put in.
    BSONElement elt;
    ASSERT_TRUE(member->getFieldDotted(fieldName, &elt));
    ASSERT_EQUALS(elt.numberInt(), 5);
}

TEST_F(WorkingSetFixture, getFieldOwned) {
    string fieldName = "x";

    BSONObj obj = BSON(fieldName << 5);
    member->obj = Snapshotted<BSONObj>(SnapshotId(), obj);
    ASSERT_TRUE(member->obj.value().isOwned());
    ws->transitionToOwnedObj(id);
    BSONElement elt;
    ASSERT_TRUE(member->getFieldDotted(fieldName, &elt));
    ASSERT_EQUALS(elt.numberInt(), 5);
}

TEST_F(WorkingSetFixture, getFieldFromIndex) {
    string firstName = "x";
    int firstValue = 5;

    string secondName = "y";
    int secondValue = 10;

    member->keyData.push_back(IndexKeyDatum(BSON(firstName << 1), BSON("" << firstValue), nullptr));
    // Also a minor lie as RecordId is bogus.
    ws->transitionToRecordIdAndIdx(id);
    BSONElement elt;
    ASSERT_TRUE(member->getFieldDotted(firstName, &elt));
    ASSERT_EQUALS(elt.numberInt(), firstValue);
    // No foo field.
    ASSERT_FALSE(member->getFieldDotted("foo", &elt));

    // Add another index datum.
    member->keyData.push_back(
        IndexKeyDatum(BSON(secondName << 1), BSON("" << secondValue), nullptr));
    ASSERT_TRUE(member->getFieldDotted(secondName, &elt));
    ASSERT_EQUALS(elt.numberInt(), secondValue);
    ASSERT_TRUE(member->getFieldDotted(firstName, &elt));
    ASSERT_EQUALS(elt.numberInt(), firstValue);
    // Still no foo.
    ASSERT_FALSE(member->getFieldDotted("foo", &elt));
}

TEST_F(WorkingSetFixture, getDottedFieldFromIndex) {
    string firstName = "x.y";
    int firstValue = 5;

    member->keyData.push_back(IndexKeyDatum(BSON(firstName << 1), BSON("" << firstValue), nullptr));
    ws->transitionToRecordIdAndIdx(id);
    BSONElement elt;
    ASSERT_TRUE(member->getFieldDotted(firstName, &elt));
    ASSERT_EQUALS(elt.numberInt(), firstValue);
    ASSERT_FALSE(member->getFieldDotted("x", &elt));
    ASSERT_FALSE(member->getFieldDotted("y", &elt));
}

}  // namespace
