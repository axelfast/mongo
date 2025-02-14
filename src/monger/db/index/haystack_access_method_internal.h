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

#include <vector>

#include "monger/db/bson/dotted_path_support.h"
#include "monger/db/catalog/collection.h"
#include "monger/db/geo/shapes.h"
#include "monger/db/record_id.h"

namespace monger {

namespace dps = ::monger::dotted_path_support;

class GeoHaystackSearchHopper {
public:
    /**
     * Constructed with a point, a max distance from that point, and a max number of
     * matched points to store.
     * @param n  The centroid that we're searching
     * @param maxDistance  The maximum distance to consider from that point
     * @param limit  The maximum number of results to return
     * @param geoField  Which field in the provided RecordId has the point to test.
     */
    GeoHaystackSearchHopper(OperationContext* opCtx,
                            const BSONObj& nearObj,
                            double maxDistance,
                            unsigned limit,
                            const std::string& geoField,
                            const Collection* collection)
        : _opCtx(opCtx),
          _collection(collection),
          _near(nearObj),
          _maxDistance(maxDistance),
          _limit(limit),
          _geoField(geoField) {}

    // Consider the point in loc, and keep it if it's within _maxDistance (and we have space for
    // it)
    void consider(const RecordId& loc) {
        if (limitReached())
            return;
        Point p(dps::extractElementAtPath(_collection->docFor(_opCtx, loc).value(), _geoField));
        if (distance(_near, p) > _maxDistance)
            return;
        _locs.push_back(loc);
    }

    int appendResultsTo(BSONArrayBuilder* b) {
        for (unsigned i = 0; i < _locs.size(); i++)
            b->append(_collection->docFor(_opCtx, _locs[i]).value());
        return _locs.size();
    }

    // Have we stored as many points as we can?
    bool limitReached() const {
        return _locs.size() >= _limit;
    }

private:
    OperationContext* _opCtx;
    const Collection* _collection;

    Point _near;
    double _maxDistance;
    unsigned _limit;
    const std::string _geoField;
    std::vector<RecordId> _locs;
};

}  // namespace monger
