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

#include <memory>

#include "monger/base/init.h"
#include "monger/bson/bsonobjbuilder.h"
#include "monger/db/catalog/index_catalog_entry.h"
#include "monger/db/index/index_descriptor.h"
#include "monger/db/json.h"
#include "monger/db/storage/kv/kv_prefix.h"
#include "monger/db/storage/sorted_data_interface_test_harness.h"
#include "monger/db/storage/wiredtiger/wiredtiger_index.h"
#include "monger/db/storage/wiredtiger/wiredtiger_record_store.h"
#include "monger/db/storage/wiredtiger/wiredtiger_recovery_unit.h"
#include "monger/db/storage/wiredtiger/wiredtiger_session_cache.h"
#include "monger/db/storage/wiredtiger/wiredtiger_util.h"
#include "monger/unittest/temp_dir.h"
#include "monger/unittest/unittest.h"

namespace monger {
namespace {

using std::string;

TEST(WiredTigerIndexTest, GenerateCreateStringEmptyDocument) {
    BSONObj spec = fromjson("{}");
    StatusWith<std::string> result = WiredTigerIndex::parseIndexOptions(spec);
    ASSERT_OK(result.getStatus());
    ASSERT_EQ(result.getValue(), "");  // "," would also be valid.
}

TEST(WiredTigerIndexTest, GenerateCreateStringUnknownField) {
    BSONObj spec = fromjson("{unknownField: 1}");
    StatusWith<std::string> result = WiredTigerIndex::parseIndexOptions(spec);
    const Status& status = result.getStatus();
    ASSERT_NOT_OK(status);
    ASSERT_EQUALS(ErrorCodes::InvalidOptions, status);
}

TEST(WiredTigerIndexTest, GenerateCreateStringNonStringConfig) {
    BSONObj spec = fromjson("{configString: 12345}");
    StatusWith<std::string> result = WiredTigerIndex::parseIndexOptions(spec);
    const Status& status = result.getStatus();
    ASSERT_NOT_OK(status);
    ASSERT_EQUALS(ErrorCodes::TypeMismatch, status);
}

TEST(WiredTigerIndexTest, GenerateCreateStringEmptyConfigString) {
    BSONObj spec = fromjson("{configString: ''}");
    StatusWith<std::string> result = WiredTigerIndex::parseIndexOptions(spec);
    ASSERT_OK(result.getStatus());
    ASSERT_EQ(result.getValue(), ",");  // "" would also be valid.
}

TEST(WiredTigerIndexTest, GenerateCreateStringInvalidConfigStringOption) {
    BSONObj spec = fromjson("{configString: 'abc=def'}");
    ASSERT_EQ(WiredTigerIndex::parseIndexOptions(spec), ErrorCodes::BadValue);
}

TEST(WiredTigerIndexTest, GenerateCreateStringValidConfigStringOption) {
    BSONObj spec = fromjson("{configString: 'prefix_compression=true'}");
    ASSERT_EQ(WiredTigerIndex::parseIndexOptions(spec), std::string("prefix_compression=true,"));
}

}  // namespace
}  // namespace monger
