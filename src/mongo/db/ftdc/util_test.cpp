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

#include "monger/db/ftdc/util.h"
#include "monger/unittest/unittest.h"

namespace monger {

void checkTime(int expected, int now_time, int period) {
    ASSERT_TRUE(Date_t::fromMillisSinceEpoch(expected) ==
                FTDCUtil::roundTime(Date_t::fromMillisSinceEpoch(now_time), Milliseconds(period)));
}

// Validate time rounding
TEST(FTDCUtilTest, TestRoundTime) {
    checkTime(4, 3, 1);
    checkTime(7, 3, 7);
    checkTime(14, 8, 7);
    checkTime(14, 13, 7);
}

// Validate the MongerS FTDC path is computed correctly from a log file path.
TEST(FTDCUtilTest, TestMongerSPath) {

    std::vector<std::pair<std::string, std::string>> testCases = {
        {"/var/log/mongers.log", "/var/log/mongers.diagnostic.data"},
        {"/var/log/mongers.foo.log", "/var/log/mongers.diagnostic.data"},
        {"/var/log/log_file", "/var/log/log_file.diagnostic.data"},
        {"./mongers.log", "./mongers.diagnostic.data"},
        {"../mongers.log", "../mongers.diagnostic.data"},
        {"c:\\var\\log\\mongers.log", "c:\\var\\log\\mongers.diagnostic.data"},
        {"c:\\var\\log\\mongers.foo.log", "c:\\var\\log\\mongers.diagnostic.data"},
        {"c:\\var\\log\\log_file", "c:\\var\\log\\log_file.diagnostic.data"},
        {"/var/some.log/mongers.log", "/var/some.log/mongers.diagnostic.data"},
        {"/var/some.log/log_file", "/var/some.log/log_file.diagnostic.data"},

        {"foo/mongers.log", "foo/mongers.diagnostic.data"},
    };

    for (const auto& p : testCases) {
        ASSERT_EQUALS(FTDCUtil::getMongerSPath(p.first), p.second);
    }
}

}  // namespace monger
