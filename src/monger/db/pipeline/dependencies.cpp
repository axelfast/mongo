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

#include "monger/db/jsobj.h"
#include "monger/db/pipeline/dependencies.h"
#include "monger/db/pipeline/field_path.h"
#include "monger/util/str.h"

namespace monger {

constexpr DepsTracker::MetadataAvailable DepsTracker::kAllGeoNearDataAvailable;

bool DepsTracker::_appendMetaProjections(BSONObjBuilder* projectionBuilder) const {
    if (_needTextScore) {
        projectionBuilder->append(Document::metaFieldTextScore,
                                  BSON("$meta"
                                       << "textScore"));
    }
    if (_needSortKey) {
        projectionBuilder->append(Document::metaFieldSortKey,
                                  BSON("$meta"
                                       << "sortKey"));
    }
    if (_needGeoNearDistance) {
        projectionBuilder->append(Document::metaFieldGeoNearDistance,
                                  BSON("$meta"
                                       << "geoNearDistance"));
    }
    if (_needGeoNearPoint) {
        projectionBuilder->append(Document::metaFieldGeoNearPoint,
                                  BSON("$meta"
                                       << "geoNearPoint"));
    }
    return (_needTextScore || _needSortKey || _needGeoNearDistance || _needGeoNearPoint);
}

BSONObj DepsTracker::toProjection() const {
    BSONObjBuilder bb;

    const bool needsMetadata = _appendMetaProjections(&bb);

    if (needWholeDocument) {
        return bb.obj();
    }

    if (fields.empty()) {
        if (needsMetadata) {
            // We only need metadata, but there is no easy way to express this in the query
            // projection language. We use $noFieldsNeeded with a meta-projection since this is an
            // inclusion projection which will exclude all existing fields but add the metadata.
            bb.append("_id", 0);
            bb.append("$noFieldsNeeded", 1);
        }
        // We either need nothing (as we would if this was logically a count), or only the metadata.
        return bb.obj();
    }

    bool needId = false;
    std::string last;
    for (const auto& field : fields) {
        if (str::startsWith(field, "_id") && (field.size() == 3 || field[3] == '.')) {
            // _id and subfields are handled specially due in part to SERVER-7502
            needId = true;
            continue;
        }

        if (!last.empty() && str::startsWith(field, last)) {
            // we are including a parent of *it so we don't need to include this field
            // explicitly. In fact, due to SERVER-6527 if we included this field, the parent
            // wouldn't be fully included.  This logic relies on on set iterators going in
            // lexicographic order so that a string is always directly before of all fields it
            // prefixes.
            continue;
        }

        last = field + '.';
        bb.append(field, 1);
    }

    if (needId)  // we are explicit either way
        bb.append("_id", 1);
    else
        bb.append("_id", 0);

    return bb.obj();
}

// ParsedDeps::_fields is a simple recursive look-up table. For each field:
//      If the value has type==Bool, the whole field is needed
//      If the value has type==Object, the fields in the subobject are needed
//      All other fields should be missing which means not needed
boost::optional<ParsedDeps> DepsTracker::toParsedDeps() const {
    MutableDocument md;

    if (needWholeDocument || _needTextScore) {
        // can't use ParsedDeps in this case
        return boost::none;
    }

    std::string last;
    for (const auto& field : fields) {
        if (!last.empty() && str::startsWith(field, last)) {
            // we are including a parent of *it so we don't need to include this field
            // explicitly. In fact, if we included this field, the parent wouldn't be fully
            // included.  This logic relies on on set iterators going in lexicographic order so
            // that a string is always directly before of all fields it prefixes.
            continue;
        }
        last = field + '.';
        md.setNestedField(field, Value(true));
    }

    return ParsedDeps(md.freeze());
}

bool DepsTracker::getNeedsMetadata(MetadataType type) const {
    switch (type) {
        case MetadataType::TEXT_SCORE:
            return _needTextScore;
        case MetadataType::SORT_KEY:
            return _needSortKey;
        case MetadataType::GEO_NEAR_DISTANCE:
            return _needGeoNearDistance;
        case MetadataType::GEO_NEAR_POINT:
            return _needGeoNearPoint;
    }
    MONGO_UNREACHABLE;
}

bool DepsTracker::isMetadataAvailable(MetadataType type) const {
    switch (type) {
        case MetadataType::TEXT_SCORE:
            return _metadataAvailable & MetadataAvailable::kTextScore;
        case MetadataType::SORT_KEY:
            MONGO_UNREACHABLE;
        case MetadataType::GEO_NEAR_DISTANCE:
            return _metadataAvailable & MetadataAvailable::kGeoNearDistance;
        case MetadataType::GEO_NEAR_POINT:
            return _metadataAvailable & MetadataAvailable::kGeoNearPoint;
    }
    MONGO_UNREACHABLE;
}

void DepsTracker::setNeedsMetadata(MetadataType type, bool required) {
    switch (type) {
        case MetadataType::TEXT_SCORE:
            uassert(40218,
                    "pipeline requires text score metadata, but there is no text score available",
                    !required || isMetadataAvailable(type));
            _needTextScore = required;
            return;
        case MetadataType::SORT_KEY:
            invariant(required || !_needSortKey);
            _needSortKey = required;
            return;
        case MetadataType::GEO_NEAR_DISTANCE:
            uassert(50860,
                    "pipeline requires $geoNear distance metadata, but it is not available",
                    !required || isMetadataAvailable(type));
            invariant(required || !_needGeoNearDistance);
            _needGeoNearDistance = required;
            return;
        case MetadataType::GEO_NEAR_POINT:
            uassert(50859,
                    "pipeline requires $geoNear point metadata, but it is not available",
                    !required || isMetadataAvailable(type));
            invariant(required || !_needGeoNearPoint);
            _needGeoNearPoint = required;
            return;
    }
    MONGO_UNREACHABLE;
}

std::vector<DepsTracker::MetadataType> DepsTracker::getAllRequiredMetadataTypes() const {
    std::vector<MetadataType> reqs;
    if (_needTextScore) {
        reqs.push_back(MetadataType::TEXT_SCORE);
    }
    if (_needSortKey) {
        reqs.push_back(MetadataType::SORT_KEY);
    }
    if (_needGeoNearDistance) {
        reqs.push_back(MetadataType::GEO_NEAR_DISTANCE);
    }
    if (_needGeoNearPoint) {
        reqs.push_back(MetadataType::GEO_NEAR_POINT);
    }
    return reqs;
}

namespace {
// Mutually recursive with arrayHelper
Document documentHelper(const BSONObj& bson, const Document& neededFields, int nFieldsNeeded = -1);

// Handles array-typed values for ParsedDeps::extractFields
Value arrayHelper(const BSONObj& bson, const Document& neededFields) {
    BSONObjIterator it(bson);

    std::vector<Value> values;
    while (it.more()) {
        BSONElement bsonElement(it.next());
        if (bsonElement.type() == Object) {
            Document sub = documentHelper(bsonElement.embeddedObject(), neededFields);
            values.push_back(Value(sub));
        }

        if (bsonElement.type() == Array) {
            values.push_back(arrayHelper(bsonElement.embeddedObject(), neededFields));
        }
    }

    return Value(std::move(values));
}

// Handles object-typed values including the top-level for ParsedDeps::extractFields
Document documentHelper(const BSONObj& bson, const Document& neededFields, int nFieldsNeeded) {
    // We cache the number of top level fields, so don't need to re-compute it every time. For
    // sub-documents, just scan for the number of fields.
    if (nFieldsNeeded == -1) {
        nFieldsNeeded = neededFields.size();
    }
    MutableDocument md(nFieldsNeeded);

    BSONObjIterator it(bson);
    while (it.more() && nFieldsNeeded > 0) {
        auto bsonElement = it.next();
        StringData fieldName = bsonElement.fieldNameStringData();
        Value isNeeded = neededFields[fieldName];

        if (isNeeded.missing())
            continue;

        --nFieldsNeeded;  // Found a needed field.
        if (isNeeded.getType() == Bool) {
            md.addField(fieldName, Value(bsonElement));
        } else {
            dassert(isNeeded.getType() == Object);

            if (bsonElement.type() == BSONType::Object) {
                md.addField(
                    fieldName,
                    Value(documentHelper(bsonElement.embeddedObject(), isNeeded.getDocument())));
            } else if (bsonElement.type() == BSONType::Array) {
                md.addField(fieldName,
                            arrayHelper(bsonElement.embeddedObject(), isNeeded.getDocument()));
            }
        }
    }

    return md.freeze();
}
}  // namespace

Document ParsedDeps::extractFields(const BSONObj& input) const {
    return documentHelper(input, _fields, _nFields);
}
}
