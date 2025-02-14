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

#include "monger/db/pipeline/document_source_add_fields.h"

#include <boost/optional.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>

#include "monger/db/pipeline/lite_parsed_document_source.h"
#include "monger/db/pipeline/parsed_add_fields.h"

namespace monger {

using boost::intrusive_ptr;
using parsed_aggregation_projection::ParsedAddFields;

REGISTER_DOCUMENT_SOURCE(addFields,
                         LiteParsedDocumentSourceDefault::parse,
                         DocumentSourceAddFields::createFromBson);
REGISTER_DOCUMENT_SOURCE(set,
                         LiteParsedDocumentSourceDefault::parse,
                         DocumentSourceAddFields::createFromBson);

intrusive_ptr<DocumentSource> DocumentSourceAddFields::create(
    BSONObj addFieldsSpec, const intrusive_ptr<ExpressionContext>& expCtx, StringData stageName) {

    const bool isIndependentOfAnyCollection = false;
    intrusive_ptr<DocumentSourceSingleDocumentTransformation> addFields(
        new DocumentSourceSingleDocumentTransformation(
            expCtx,
            ParsedAddFields::create(expCtx, addFieldsSpec),
            stageName.toString(),
            isIndependentOfAnyCollection));
    return addFields;
}

intrusive_ptr<DocumentSource> DocumentSourceAddFields::createFromBson(
    BSONElement elem, const intrusive_ptr<ExpressionContext>& expCtx) {
    const auto specifiedName = elem.fieldNameStringData();
    invariant(specifiedName == kStageName || specifiedName == kAliasNameSet);

    uassert(40272,
            str::stream() << specifiedName << " specification stage must be an object, got "
                          << typeName(elem.type()),
            elem.type() == Object);

    return DocumentSourceAddFields::create(elem.Obj(), expCtx, specifiedName);
}
}
