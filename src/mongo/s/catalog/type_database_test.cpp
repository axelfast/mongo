/**
 *    Copyright (C) 2018-present MongerDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongerDB, Inc.
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
#include "monger/s/catalog/type_database.h"
#include "monger/unittest/unittest.h"
#include "monger/util/uuid.h"

namespace {

using namespace monger;
using std::string;

TEST(DatabaseType, Empty) {
    StatusWith<DatabaseType> status = DatabaseType::fromBSON(BSONObj());
    ASSERT_FALSE(status.isOK());
}

TEST(DatabaseType, Basic) {
    UUID uuid = UUID::gen();
    StatusWith<DatabaseType> status = DatabaseType::fromBSON(
        BSON(DatabaseType::name("mydb")
             << DatabaseType::primary("shard")
             << DatabaseType::sharded(true)
             << DatabaseType::version(BSON("uuid" << uuid << "lastMod" << 0))));
    ASSERT_TRUE(status.isOK());

    DatabaseType db = status.getValue();
    ASSERT_EQUALS(db.getName(), "mydb");
    ASSERT_EQUALS(db.getPrimary(), "shard");
    ASSERT_TRUE(db.getSharded());
    ASSERT_EQUALS(db.getVersion().getUuid(), uuid);
    ASSERT_EQUALS(db.getVersion().getLastMod(), 0);
}

TEST(DatabaseType, BadType) {
    StatusWith<DatabaseType> status = DatabaseType::fromBSON(BSON(DatabaseType::name() << 0));
    ASSERT_FALSE(status.isOK());
}

TEST(DatabaseType, MissingRequired) {
    StatusWith<DatabaseType> status = DatabaseType::fromBSON(BSON(DatabaseType::name("mydb")));
    ASSERT_FALSE(status.isOK());
}

}  // unnamed namespace
