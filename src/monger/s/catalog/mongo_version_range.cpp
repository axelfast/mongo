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

#include "monger/s/catalog/monger_version_range.h"

#include "monger/util/str.h"

namespace monger {

using std::string;
using std::vector;

BSONArray MongerVersionRange::toBSONArray(const vector<MongerVersionRange>& ranges) {
    BSONArrayBuilder barr;

    for (vector<MongerVersionRange>::const_iterator it = ranges.begin(); it != ranges.end(); ++it) {
        const MongerVersionRange& range = *it;
        range.toBSONElement(&barr);
    }

    return barr.arr();
}

bool MongerVersionRange::parseBSONElement(const BSONElement& el, string* errMsg) {
    string dummy;
    if (!errMsg)
        errMsg = &dummy;

    if (el.type() == String) {
        minVersion = el.String();
        if (minVersion == "") {
            *errMsg = (string) "cannot parse single empty monger version (" + el.toString() + ")";
            return false;
        }
        return true;
    } else if (el.type() == Array || el.type() == Object) {
        BSONObj range = el.Obj();

        if (range.nFields() != 2) {
            *errMsg = (string) "not enough fields in monger version range (" + el.toString() + ")";
            return false;
        }

        BSONObjIterator it(range);

        BSONElement subElA = it.next();
        BSONElement subElB = it.next();

        if (subElA.type() != String || subElB.type() != String) {
            *errMsg = (string) "wrong field type for monger version range (" + el.toString() + ")";
            return false;
        }

        minVersion = subElA.String();
        maxVersion = subElB.String();

        if (minVersion == "") {
            *errMsg = (string) "cannot parse first empty monger version (" + el.toString() + ")";
            return false;
        }

        if (maxVersion == "") {
            *errMsg = (string) "cannot parse second empty monger version (" + el.toString() + ")";
            return false;
        }

        if (str::versionCmp(minVersion, maxVersion) > 0) {
            string swap = minVersion;
            minVersion = maxVersion;
            maxVersion = swap;
        }

        return true;
    } else {
        *errMsg = (string) "wrong type for monger version range " + el.toString();
        return false;
    }
}

void MongerVersionRange::toBSONElement(BSONArrayBuilder* barr) const {
    if (maxVersion == "") {
        barr->append(minVersion);
    } else {
        BSONArrayBuilder rangeB(barr->subarrayStart());

        rangeB.append(minVersion);
        rangeB.append(maxVersion);

        rangeB.done();
    }
}

bool MongerVersionRange::isInRange(StringData version) const {
    if (maxVersion == "") {
        // If a prefix of the version specified is excluded, the specified version is
        // excluded
        if (version.find(minVersion) == 0)
            return true;
    } else {
        // Range is inclusive, so make sure the end and beginning prefix excludes all
        // prefixed versions as above
        if (version.find(minVersion) == 0)
            return true;
        if (version.find(maxVersion) == 0)
            return true;
        if (str::versionCmp(minVersion, version) <= 0 &&
            str::versionCmp(maxVersion, version) >= 0) {
            return true;
        }
    }

    return false;
}

bool isInMongerVersionRanges(StringData version, const vector<MongerVersionRange>& ranges) {
    for (const auto& r : ranges) {
        if (r.isInRange(version))
            return true;
    }

    return false;
}
}
