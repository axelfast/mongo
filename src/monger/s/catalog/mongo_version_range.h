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

#pragma once

#include <string>
#include <vector>

#include "monger/base/string_data.h"
#include "monger/db/jsobj.h"

namespace monger {

/**
 * The MongerVersionRange represents a min/max of MongerDB versions, useful for
 * excluding/including particular versions.
 *
 * The ranges may be single-version, in which case maxVersion == "", where only exact prefix
 * matches are included in the range.  Alternately, the range may have a min and max version
 * and include any version with a prefix of the min and max version as well as all versions
 * between the two.
 */
struct MongerVersionRange {
    static BSONArray toBSONArray(const std::vector<MongerVersionRange>& ranges);

    bool parseBSONElement(const BSONElement& el, std::string* errMsg);

    void toBSONElement(BSONArrayBuilder* barr) const;

    bool isInRange(StringData version) const;

    std::string minVersion;
    std::string maxVersion;
};

bool isInMongerVersionRanges(StringData version, const std::vector<MongerVersionRange>& ranges);
}
