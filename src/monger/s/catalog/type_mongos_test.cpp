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

#include "monger/base/status_with.h"
#include "monger/db/jsobj.h"
#include "monger/s/catalog/type_mongers.h"
#include "monger/unittest/unittest.h"
#include "monger/util/time_support.h"

namespace {

using namespace monger;

TEST(Validity, MissingName) {
    BSONObj obj = BSON(MongersType::ping(Date_t::fromMillisSinceEpoch(1))
                       << MongersType::uptime(100)
                       << MongersType::waiting(false)
                       << MongersType::mongerVersion("x.x.x")
                       << MongersType::configVersion(0)
                       << MongersType::advisoryHostFQDNs(BSONArrayBuilder().arr()));

    auto mongersTypeResult = MongersType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::NoSuchKey, mongersTypeResult.getStatus());
}

TEST(Validity, MissingPing) {
    BSONObj obj = BSON(MongersType::name("localhost:27017")
                       << MongersType::uptime(100)
                       << MongersType::waiting(false)
                       << MongersType::mongerVersion("x.x.x")
                       << MongersType::configVersion(0)
                       << MongersType::advisoryHostFQDNs(BSONArrayBuilder().arr()));

    auto mongersTypeResult = MongersType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::NoSuchKey, mongersTypeResult.getStatus());
}

TEST(Validity, MissingUp) {
    BSONObj obj = BSON(MongersType::name("localhost:27017")
                       << MongersType::ping(Date_t::fromMillisSinceEpoch(1))
                       << MongersType::waiting(false)
                       << MongersType::mongerVersion("x.x.x")
                       << MongersType::configVersion(0)
                       << MongersType::advisoryHostFQDNs(BSONArrayBuilder().arr()));

    auto mongersTypeResult = MongersType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::NoSuchKey, mongersTypeResult.getStatus());
}

TEST(Validity, MissingWaiting) {
    BSONObj obj = BSON(MongersType::name("localhost:27017")
                       << MongersType::ping(Date_t::fromMillisSinceEpoch(1))
                       << MongersType::uptime(100)
                       << MongersType::mongerVersion("x.x.x")
                       << MongersType::configVersion(0)
                       << MongersType::advisoryHostFQDNs(BSONArrayBuilder().arr()));

    auto mongersTypeResult = MongersType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::NoSuchKey, mongersTypeResult.getStatus());
}

TEST(Validity, MissingMongerVersion) {
    BSONObj obj = BSON(MongersType::name("localhost:27017")
                       << MongersType::ping(Date_t::fromMillisSinceEpoch(1))
                       << MongersType::uptime(100)
                       << MongersType::waiting(false)
                       << MongersType::configVersion(0)
                       << MongersType::advisoryHostFQDNs(BSONArrayBuilder().arr()));

    auto mongersTypeResult = MongersType::fromBSON(obj);
    ASSERT_OK(mongersTypeResult.getStatus());

    MongersType& mtype = mongersTypeResult.getValue();
    /**
     * Note: mongerVersion should eventually become mandatory, but is optional now
     *       for backward compatibility reasons.
     */
    ASSERT_OK(mtype.validate());
}

TEST(Validity, MissingConfigVersion) {
    BSONObj obj = BSON(MongersType::name("localhost:27017")
                       << MongersType::ping(Date_t::fromMillisSinceEpoch(1))
                       << MongersType::uptime(100)
                       << MongersType::waiting(false)
                       << MongersType::mongerVersion("x.x.x")
                       << MongersType::advisoryHostFQDNs(BSONArrayBuilder().arr()));

    auto mongersTypeResult = MongersType::fromBSON(obj);
    ASSERT_OK(mongersTypeResult.getStatus());

    MongersType& mtype = mongersTypeResult.getValue();
    /**
     * Note: configVersion should eventually become mandatory, but is optional now
     *       for backward compatibility reasons.
     */
    ASSERT_OK(mtype.validate());
}

TEST(Validity, MissingAdvisoryHostFQDNs) {
    BSONObj obj = BSON(MongersType::name("localhost:27017")
                       << MongersType::ping(Date_t::fromMillisSinceEpoch(1))
                       << MongersType::uptime(100)
                       << MongersType::waiting(false)
                       << MongersType::mongerVersion("x.x.x")
                       << MongersType::configVersion(0));

    auto mongersTypeResult = MongersType::fromBSON(obj);
    ASSERT_OK(mongersTypeResult.getStatus());

    MongersType& mType = mongersTypeResult.getValue();
    // advisoryHostFQDNs is optional
    ASSERT_OK(mType.validate());
}

TEST(Validity, EmptyAdvisoryHostFQDNs) {
    BSONObj obj = BSON(MongersType::name("localhost:27017")
                       << MongersType::ping(Date_t::fromMillisSinceEpoch(1))
                       << MongersType::uptime(100)
                       << MongersType::waiting(false)
                       << MongersType::mongerVersion("x.x.x")
                       << MongersType::configVersion(0)
                       << MongersType::advisoryHostFQDNs(BSONArrayBuilder().arr()));

    auto mongersTypeResult = MongersType::fromBSON(obj);
    ASSERT_OK(mongersTypeResult.getStatus());

    MongersType& mType = mongersTypeResult.getValue();
    ASSERT_OK(mType.validate());

    ASSERT_EQUALS(mType.getAdvisoryHostFQDNs().size(), 0UL);
}

TEST(Validity, BadTypeAdvisoryHostFQDNs) {
    BSONObj obj = BSON(MongersType::name("localhost:27017")
                       << MongersType::ping(Date_t::fromMillisSinceEpoch(1))
                       << MongersType::uptime(100)
                       << MongersType::waiting(false)
                       << MongersType::mongerVersion("x.x.x")
                       << MongersType::configVersion(0)
                       << MongersType::advisoryHostFQDNs(BSON_ARRAY("foo" << 0 << "baz")));

    auto mongersTypeResult = MongersType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::TypeMismatch, mongersTypeResult.getStatus());
}

TEST(Validity, Valid) {
    BSONObj obj = BSON(MongersType::name("localhost:27017")
                       << MongersType::ping(Date_t::fromMillisSinceEpoch(1))
                       << MongersType::uptime(100)
                       << MongersType::waiting(false)
                       << MongersType::mongerVersion("x.x.x")
                       << MongersType::configVersion(0)
                       << MongersType::advisoryHostFQDNs(BSON_ARRAY("foo"
                                                                   << "bar"
                                                                   << "baz")));

    auto mongersTypeResult = MongersType::fromBSON(obj);
    ASSERT_OK(mongersTypeResult.getStatus());

    MongersType& mType = mongersTypeResult.getValue();
    ASSERT_OK(mType.validate());

    ASSERT_EQUALS(mType.getName(), "localhost:27017");
    ASSERT_EQUALS(mType.getPing(), Date_t::fromMillisSinceEpoch(1));
    ASSERT_EQUALS(mType.getUptime(), 100);
    ASSERT_EQUALS(mType.getWaiting(), false);
    ASSERT_EQUALS(mType.getMongerVersion(), "x.x.x");
    ASSERT_EQUALS(mType.getConfigVersion(), 0);
    ASSERT_EQUALS(mType.getAdvisoryHostFQDNs().size(), 3UL);
    ASSERT_EQUALS(mType.getAdvisoryHostFQDNs()[0], "foo");
    ASSERT_EQUALS(mType.getAdvisoryHostFQDNs()[1], "bar");
    ASSERT_EQUALS(mType.getAdvisoryHostFQDNs()[2], "baz");
}

TEST(Validity, BadType) {
    BSONObj obj = BSON(MongersType::name() << 0);

    auto mongersTypeResult = MongersType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::TypeMismatch, mongersTypeResult.getStatus());
}

}  // unnamed namespace
