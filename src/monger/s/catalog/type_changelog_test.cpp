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
#include "monger/s/catalog/type_changelog.h"
#include "monger/unittest/unittest.h"
#include "monger/util/time_support.h"

namespace {

using namespace monger;

TEST(ChangeLogType, Empty) {
    auto changeLogResult = ChangeLogType::fromBSON(BSONObj());
    ASSERT_NOT_OK(changeLogResult.getStatus());
}

TEST(ChangeLogType, Valid) {
    BSONObj obj = BSON(ChangeLogType::changeId("host.local-2012-11-21T19:14:10-8")
                       << ChangeLogType::server("host.local")
                       << ChangeLogType::shard("shardname")
                       << ChangeLogType::clientAddr("192.168.0.189:51128")
                       << ChangeLogType::time(Date_t::fromMillisSinceEpoch(1))
                       << ChangeLogType::what("split")
                       << ChangeLogType::ns("test.test")
                       << ChangeLogType::details(BSON("dummy"
                                                      << "info")));

    auto changeLogResult = ChangeLogType::fromBSON(obj);
    ASSERT_OK(changeLogResult.getStatus());
    ChangeLogType& logEntry = changeLogResult.getValue();
    ASSERT_OK(logEntry.validate());

    ASSERT_EQUALS(logEntry.getChangeId(), "host.local-2012-11-21T19:14:10-8");
    ASSERT_EQUALS(logEntry.getServer(), "host.local");
    ASSERT_EQUALS(logEntry.getShard(), "shardname");
    ASSERT_EQUALS(logEntry.getClientAddr(), "192.168.0.189:51128");
    ASSERT_EQUALS(logEntry.getTime(), Date_t::fromMillisSinceEpoch(1));
    ASSERT_EQUALS(logEntry.getWhat(), "split");
    ASSERT_EQUALS(logEntry.getNS(), "test.test");
    ASSERT_BSONOBJ_EQ(logEntry.getDetails(),
                      BSON("dummy"
                           << "info"));
}

TEST(ChangeLogType, MissingChangeId) {
    BSONObj obj = BSON(ChangeLogType::server("host.local")
                       << ChangeLogType::shard("shardname")
                       << ChangeLogType::clientAddr("192.168.0.189:51128")
                       << ChangeLogType::time(Date_t::fromMillisSinceEpoch(1))
                       << ChangeLogType::what("split")
                       << ChangeLogType::ns("test.test")
                       << ChangeLogType::details(BSON("dummy"
                                                      << "info")));

    auto changeLogResult = ChangeLogType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::NoSuchKey, changeLogResult.getStatus());
}

TEST(ChangeLogType, MissingServer) {
    BSONObj obj = BSON(ChangeLogType::changeId("host.local-2012-11-21T19:14:10-8")
                       << ChangeLogType::shard("shardname")
                       << ChangeLogType::clientAddr("192.168.0.189:51128")
                       << ChangeLogType::time(Date_t::fromMillisSinceEpoch(1))
                       << ChangeLogType::what("split")
                       << ChangeLogType::ns("test.test")
                       << ChangeLogType::details(BSON("dummy"
                                                      << "info")));

    auto changeLogResult = ChangeLogType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::NoSuchKey, changeLogResult.getStatus());
}

TEST(ChangeLogType, MissingClientAddr) {
    BSONObj obj = BSON(ChangeLogType::changeId("host.local-2012-11-21T19:14:10-8")
                       << ChangeLogType::server("host.local")
                       << ChangeLogType::shard("shardname")
                       << ChangeLogType::time(Date_t::fromMillisSinceEpoch(1))
                       << ChangeLogType::what("split")
                       << ChangeLogType::ns("test.test")
                       << ChangeLogType::details(BSON("dummy"
                                                      << "info")));

    auto changeLogResult = ChangeLogType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::NoSuchKey, changeLogResult.getStatus());
}

TEST(ChangeLogType, MissingTime) {
    BSONObj obj = BSON(ChangeLogType::changeId("host.local-2012-11-21T19:14:10-8")
                       << ChangeLogType::server("host.local")
                       << ChangeLogType::shard("shardname")
                       << ChangeLogType::clientAddr("192.168.0.189:51128")
                       << ChangeLogType::what("split")
                       << ChangeLogType::ns("test.test")
                       << ChangeLogType::details(BSON("dummy"
                                                      << "info")));

    auto changeLogResult = ChangeLogType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::NoSuchKey, changeLogResult.getStatus());
}

TEST(ChangeLogType, MissingWhat) {
    BSONObj obj = BSON(ChangeLogType::changeId("host.local-2012-11-21T19:14:10-8")
                       << ChangeLogType::server("host.local")
                       << ChangeLogType::shard("shardname")
                       << ChangeLogType::clientAddr("192.168.0.189:51128")
                       << ChangeLogType::time(Date_t::fromMillisSinceEpoch(1))
                       << ChangeLogType::ns("test.test")
                       << ChangeLogType::details(BSON("dummy"
                                                      << "info")));

    auto changeLogResult = ChangeLogType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::NoSuchKey, changeLogResult.getStatus());
}

TEST(ChangeLogType, MissingNS) {
    BSONObj obj = BSON(ChangeLogType::changeId("host.local-2012-11-21T19:14:10-8")
                       << ChangeLogType::server("host.local")
                       << ChangeLogType::shard("shardname")
                       << ChangeLogType::clientAddr("192.168.0.189:51128")
                       << ChangeLogType::time(Date_t::fromMillisSinceEpoch(1))
                       << ChangeLogType::what("split")
                       << ChangeLogType::details(BSON("dummy"
                                                      << "info")));

    auto changeLogResult = ChangeLogType::fromBSON(obj);
    ASSERT_OK(changeLogResult.getStatus());
    ChangeLogType& logEntry = changeLogResult.getValue();
    ASSERT_OK(logEntry.validate());

    ASSERT_EQUALS(logEntry.getChangeId(), "host.local-2012-11-21T19:14:10-8");
    ASSERT_EQUALS(logEntry.getServer(), "host.local");
    ASSERT_EQUALS(logEntry.getShard(), "shardname");
    ASSERT_EQUALS(logEntry.getClientAddr(), "192.168.0.189:51128");
    ASSERT_EQUALS(logEntry.getTime(), Date_t::fromMillisSinceEpoch(1));
    ASSERT_EQUALS(logEntry.getWhat(), "split");
    ASSERT_BSONOBJ_EQ(logEntry.getDetails(),
                      BSON("dummy"
                           << "info"));
}

TEST(ChangeLogType, MissingDetails) {
    BSONObj obj = BSON(ChangeLogType::changeId("host.local-2012-11-21T19:14:10-8")
                       << ChangeLogType::server("host.local")
                       << ChangeLogType::shard("shardname")
                       << ChangeLogType::clientAddr("192.168.0.189:51128")
                       << ChangeLogType::time(Date_t::fromMillisSinceEpoch(1))
                       << ChangeLogType::what("split")
                       << ChangeLogType::ns("test.test"));

    auto changeLogResult = ChangeLogType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::NoSuchKey, changeLogResult.getStatus());
}

TEST(ChangeLogType, MissingShard) {
    BSONObj obj = BSON(ChangeLogType::changeId("host.local-2012-11-21T19:14:10-8")
                       << ChangeLogType::server("host.local")
                       << ChangeLogType::clientAddr("192.168.0.189:51128")
                       << ChangeLogType::time(Date_t::fromMillisSinceEpoch(1))
                       << ChangeLogType::what("split")
                       << ChangeLogType::ns("test.test")
                       << ChangeLogType::details(BSON("dummy"
                                                      << "info")));

    auto changeLogResult = ChangeLogType::fromBSON(obj);
    ASSERT_OK(changeLogResult.getStatus());
    ChangeLogType& logEntry = changeLogResult.getValue();
    ASSERT_OK(logEntry.validate());

    ASSERT_EQUALS(logEntry.getChangeId(), "host.local-2012-11-21T19:14:10-8");
    ASSERT_EQUALS(logEntry.getServer(), "host.local");
    ASSERT_EQUALS(logEntry.getClientAddr(), "192.168.0.189:51128");
    ASSERT_EQUALS(logEntry.getTime(), Date_t::fromMillisSinceEpoch(1));
    ASSERT_EQUALS(logEntry.getWhat(), "split");
    ASSERT_EQUALS(logEntry.getNS(), "test.test");
    ASSERT_BSONOBJ_EQ(logEntry.getDetails(),
                      BSON("dummy"
                           << "info"));
}

TEST(ChangeLogType, BadType) {
    ChangeLogType logEntry;
    BSONObj obj = BSON(ChangeLogType::changeId() << 0);
    auto changeLogResult = ChangeLogType::fromBSON(obj);
    ASSERT_EQ(ErrorCodes::TypeMismatch, changeLogResult.getStatus());
}

}  // unnamed namespace
