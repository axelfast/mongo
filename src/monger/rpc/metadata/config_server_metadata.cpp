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

#include "monger/rpc/metadata/config_server_metadata.h"

#include "monger/bson/util/bson_check.h"
#include "monger/db/jsobj.h"
#include "monger/db/repl/bson_extract_optime.h"
#include "monger/rpc/metadata.h"

namespace monger {
namespace rpc {

using repl::OpTime;

namespace {

const char kOpTimeFieldName[] = "opTime";

}  // unnamed namespace

const OperationContext::Decoration<ConfigServerMetadata> ConfigServerMetadata::get =
    OperationContext::declareDecoration<ConfigServerMetadata>();

ConfigServerMetadata::ConfigServerMetadata(OpTime opTime) : _opTime(std::move(opTime)) {}

StatusWith<ConfigServerMetadata> ConfigServerMetadata::readFromMetadata(
    const BSONObj& metadataObj) {
    return readFromMetadata(metadataObj.getField(fieldName()));
}

StatusWith<ConfigServerMetadata> ConfigServerMetadata::readFromMetadata(
    const BSONElement& metadataElem) {
    if (metadataElem.eoo()) {
        return ConfigServerMetadata{};
    } else if (metadataElem.type() != monger::Object) {
        return {ErrorCodes::TypeMismatch,
                str::stream() << "ConfigServerMetadata element has incorrect type: expected"
                              << monger::Object
                              << " but got "
                              << metadataElem.type()};
    }

    BSONObj configMetadataObj = metadataElem.Obj();

    repl::OpTime opTime;
    auto status = bsonExtractOpTimeField(configMetadataObj, kOpTimeFieldName, &opTime);
    if (!status.isOK()) {
        return status;
    }

    return ConfigServerMetadata(std::move(opTime));
}

void ConfigServerMetadata::writeToMetadata(BSONObjBuilder* builder) const {
    invariant(_opTime);
    BSONObjBuilder configMetadataBuilder(builder->subobjStart(fieldName()));
    _opTime->append(&configMetadataBuilder, kOpTimeFieldName);
}

}  // namespace rpc
}  // namespace monger
