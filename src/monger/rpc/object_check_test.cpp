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

#include <iterator>

#include "monger/base/data_range_cursor.h"
#include "monger/db/jsobj.h"
#include "monger/db/server_options.h"
#include "monger/rpc/object_check.h"
#include "monger/unittest/unittest.h"
#include "monger/util/scopeguard.h"

namespace {

using namespace monger;

TEST(DataTypeValidated, BSONValidationEnabled) {
    using std::swap;

    bool wasEnabled = serverGlobalParams.objcheck;
    const auto setValidation = [&](bool enabled) { serverGlobalParams.objcheck = enabled; };
    ON_BLOCK_EXIT([=] { setValidation(wasEnabled); });

    using std::begin;
    using std::end;

    BSONObj valid = BSON("baz"
                         << "bar"
                         << "garply"
                         << BSON("foo"
                                 << "bar"));
    char buf[1024] = {0};
    std::copy(valid.objdata(), valid.objdata() + valid.objsize(), begin(buf));
    {
        Validated<BSONObj> v;
        ConstDataRangeCursor cdrc(begin(buf), end(buf));
        ASSERT_OK(cdrc.readAndAdvanceNoThrow(&v));
    }

    {
        // mess up the data
        DataRangeCursor drc(begin(buf), end(buf));
        // skip past size so we don't trip any sanity checks.
        drc.advanceNoThrow(4).transitional_ignore();  // skip size
        while (drc.writeAndAdvanceNoThrow(0xFF).isOK())
            ;
    }

    {
        Validated<BSONObj> v;
        ConstDataRangeCursor cdrc(begin(buf), end(buf));
        ASSERT_NOT_OK(cdrc.readAndAdvanceNoThrow(&v));
    }

    {
        // disable validation
        setValidation(false);
        Validated<BSONObj> v;
        ConstDataRangeCursor cdrc(begin(buf), end(buf));
        ASSERT_OK(cdrc.readAndAdvanceNoThrow(&v));
    }
}
}
