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

#include "monger/db/index_legacy.h"

#include <string>

#include "monger/db/catalog/collection.h"
#include "monger/db/catalog/index_catalog.h"
#include "monger/db/client.h"
#include "monger/db/fts/fts_spec.h"
#include "monger/db/index/expression_keys_private.h"
#include "monger/db/index/s2_access_method.h"
#include "monger/db/index_names.h"
#include "monger/db/jsobj.h"

namespace monger {

// static
StatusWith<BSONObj> IndexLegacy::adjustIndexSpecObject(const BSONObj& obj) {
    std::string pluginName = IndexNames::findPluginName(obj.getObjectField("key"));

    if (IndexNames::TEXT == pluginName) {
        return fts::FTSSpec::fixSpec(obj);
    }

    if (IndexNames::GEO_2DSPHERE == pluginName) {
        return S2AccessMethod::fixSpec(obj);
    }

    return obj;
}

// static
BSONObj IndexLegacy::getMissingField(Collection* collection, const BSONObj& infoObj) {
    BSONObj keyPattern = infoObj.getObjectField("key");
    std::string accessMethodName;
    if (collection)
        accessMethodName = collection->getIndexCatalog()->getAccessMethodName(keyPattern);
    else
        accessMethodName = IndexNames::findPluginName(keyPattern);

    if (IndexNames::HASHED == accessMethodName) {
        int hashVersion = infoObj["hashVersion"].numberInt();
        HashSeed seed = infoObj["seed"].numberInt();

        // Explicit null valued fields and missing fields are both represented in hashed indexes
        // using the hash value of the null BSONElement.  This is partly for historical reasons
        // (hash of null was used in the initial release of hashed indexes and changing would
        // alter the data format).  Additionally, in certain places the hashed index code and
        // the index bound calculation code assume null and missing are indexed identically.
        BSONObj nullObj = BSON("" << BSONNULL);
        return BSON("" << ExpressionKeysPrivate::makeSingleHashKey(
                        nullObj.firstElement(), seed, hashVersion));
    } else {
        BSONObjBuilder b;
        b.appendNull("");
        return b.obj();
    }
}

}  // namespace monger
