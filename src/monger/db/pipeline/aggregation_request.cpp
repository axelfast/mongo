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

#include "monger/db/pipeline/aggregation_request.h"

#include <algorithm>

#include "monger/base/error_codes.h"
#include "monger/base/status_with.h"
#include "monger/base/string_data.h"
#include "monger/db/catalog/document_validation.h"
#include "monger/db/command_generic_argument.h"
#include "monger/db/commands.h"
#include "monger/db/pipeline/document.h"
#include "monger/db/pipeline/value.h"
#include "monger/db/query/cursor_request.h"
#include "monger/db/query/query_request.h"
#include "monger/db/repl/read_concern_args.h"
#include "monger/db/storage/storage_options.h"

namespace monger {

constexpr StringData AggregationRequest::kCommandName;
constexpr StringData AggregationRequest::kCursorName;
constexpr StringData AggregationRequest::kBatchSizeName;
constexpr StringData AggregationRequest::kFromMongersName;
constexpr StringData AggregationRequest::kNeedsMergeName;
constexpr StringData AggregationRequest::kPipelineName;
constexpr StringData AggregationRequest::kCollationName;
constexpr StringData AggregationRequest::kExplainName;
constexpr StringData AggregationRequest::kAllowDiskUseName;
constexpr StringData AggregationRequest::kHintName;
constexpr StringData AggregationRequest::kCommentName;
constexpr StringData AggregationRequest::kExchangeName;

constexpr long long AggregationRequest::kDefaultBatchSize;

StatusWith<AggregationRequest> AggregationRequest::parseFromBSON(
    const std::string& dbName,
    const BSONObj& cmdObj,
    boost::optional<ExplainOptions::Verbosity> explainVerbosity) {
    return parseFromBSON(parseNs(dbName, cmdObj), cmdObj, explainVerbosity);
}

StatusWith<AggregationRequest> AggregationRequest::parseFromBSON(
    NamespaceString nss,
    const BSONObj& cmdObj,
    boost::optional<ExplainOptions::Verbosity> explainVerbosity) {
    // Parse required parameters.
    auto pipelineElem = cmdObj[kPipelineName];
    auto pipeline = AggregationRequest::parsePipelineFromBSON(pipelineElem);
    if (!pipeline.isOK()) {
        return pipeline.getStatus();
    }

    AggregationRequest request(std::move(nss), std::move(pipeline.getValue()));

    const std::initializer_list<StringData> optionsParsedElseWhere = {kPipelineName, kCommandName};

    bool hasCursorElem = false;
    bool hasExplainElem = false;

    bool hasFromMongersElem = false;
    bool hasNeedsMergeElem = false;

    // Parse optional parameters.
    for (auto&& elem : cmdObj) {
        auto fieldName = elem.fieldNameStringData();

        if (QueryRequest::kUnwrappedReadPrefField == fieldName) {
            // We expect this field to be validated elsewhere.
            request.setUnwrappedReadPref(elem.embeddedObject());
        } else if (std::find(optionsParsedElseWhere.begin(),
                             optionsParsedElseWhere.end(),
                             fieldName) != optionsParsedElseWhere.end()) {
            // Ignore options that are parsed elsewhere.
        } else if (kCursorName == fieldName) {
            long long batchSize;
            auto status =
                CursorRequest::parseCommandCursorOptions(cmdObj, kDefaultBatchSize, &batchSize);
            if (!status.isOK()) {
                return status;
            }

            hasCursorElem = true;
            request.setBatchSize(batchSize);
        } else if (kCollationName == fieldName) {
            if (elem.type() != BSONType::Object) {
                return {ErrorCodes::TypeMismatch,
                        str::stream() << kCollationName << " must be an object, not a "
                                      << typeName(elem.type())};
            }
            request.setCollation(elem.embeddedObject().getOwned());
        } else if (QueryRequest::cmdOptionMaxTimeMS == fieldName) {
            auto maxTimeMs = QueryRequest::parseMaxTimeMS(elem);
            if (!maxTimeMs.isOK()) {
                return maxTimeMs.getStatus();
            }

            request.setMaxTimeMS(maxTimeMs.getValue());
        } else if (repl::ReadConcernArgs::kReadConcernFieldName == fieldName) {
            if (elem.type() != BSONType::Object) {
                return {ErrorCodes::TypeMismatch,
                        str::stream() << repl::ReadConcernArgs::kReadConcernFieldName
                                      << " must be an object, not a "
                                      << typeName(elem.type())};
            }
            request.setReadConcern(elem.embeddedObject().getOwned());
        } else if (kHintName == fieldName) {
            if (BSONType::Object == elem.type()) {
                request.setHint(elem.embeddedObject());
            } else if (BSONType::String == elem.type()) {
                request.setHint(BSON("$hint" << elem.valueStringData()));
            } else {
                return Status(ErrorCodes::FailedToParse,
                              str::stream()
                                  << kHintName
                                  << " must be specified as a string representing an index"
                                  << " name, or an object representing an index's key pattern");
            }
        } else if (kCommentName == fieldName) {
            if (elem.type() != BSONType::String) {
                return {ErrorCodes::TypeMismatch,
                        str::stream() << kCommentName << " must be a string, not a "
                                      << typeName(elem.type())};
            }
            request.setComment(elem.str());
        } else if (kExplainName == fieldName) {
            if (elem.type() != BSONType::Bool) {
                return {ErrorCodes::TypeMismatch,
                        str::stream() << kExplainName << " must be a boolean, not a "
                                      << typeName(elem.type())};
            }

            hasExplainElem = true;
            if (elem.Bool()) {
                request.setExplain(ExplainOptions::Verbosity::kQueryPlanner);
            }
        } else if (kFromMongersName == fieldName) {
            if (elem.type() != BSONType::Bool) {
                return {ErrorCodes::TypeMismatch,
                        str::stream() << kFromMongersName << " must be a boolean, not a "
                                      << typeName(elem.type())};
            }

            hasFromMongersElem = true;
            request.setFromMongers(elem.Bool());
        } else if (kNeedsMergeName == fieldName) {
            if (elem.type() != BSONType::Bool) {
                return {ErrorCodes::TypeMismatch,
                        str::stream() << kNeedsMergeName << " must be a boolean, not a "
                                      << typeName(elem.type())};
            }

            hasNeedsMergeElem = true;
            request.setNeedsMerge(elem.Bool());
        } else if (kAllowDiskUseName == fieldName) {
            if (storageGlobalParams.readOnly) {
                return {ErrorCodes::IllegalOperation,
                        str::stream() << "The '" << kAllowDiskUseName
                                      << "' option is not permitted in read-only mode."};
            } else if (elem.type() != BSONType::Bool) {
                return {ErrorCodes::TypeMismatch,
                        str::stream() << kAllowDiskUseName << " must be a boolean, not a "
                                      << typeName(elem.type())};
            }
            request.setAllowDiskUse(elem.Bool());
        } else if (kExchangeName == fieldName) {
            try {
                IDLParserErrorContext ctx("internalExchange");
                request.setExchangeSpec(ExchangeSpec::parse(ctx, elem.Obj()));
            } catch (const DBException& ex) {
                return ex.toStatus();
            }
        } else if (bypassDocumentValidationCommandOption() == fieldName) {
            request.setBypassDocumentValidation(elem.trueValue());
        } else if (WriteConcernOptions::kWriteConcernField == fieldName) {
            if (elem.type() != BSONType::Object) {
                return {ErrorCodes::TypeMismatch,
                        str::stream() << fieldName << " must be an object, not a "
                                      << typeName(elem.type())};
            }

            WriteConcernOptions writeConcern;
            uassertStatusOK(writeConcern.parse(elem.embeddedObject()));
            request.setWriteConcern(writeConcern);
        } else if (kRuntimeConstants == fieldName) {
            try {
                IDLParserErrorContext ctx("internalRuntimeConstants");
                request.setRuntimeConstants(RuntimeConstants::parse(ctx, elem.Obj()));
            } catch (const DBException& ex) {
                return ex.toStatus();
            }
        } else if (fieldName == "mergeByPBRT"_sd) {
            // TODO SERVER-41900: we must retain the ability to ingest the 'mergeByPBRT' field for
            // 4.4 upgrade purposes, since a 4.2 mongerS will always send {mergeByPBRT:true} to the
            // shards. We do nothing with it because mergeByPBRT is the only mode available in 4.4.
            // Remove this final vestige of mergeByPBRT during the 4.5 development cycle.
        } else if (!isGenericArgument(fieldName)) {
            return {ErrorCodes::FailedToParse,
                    str::stream() << "unrecognized field '" << elem.fieldName() << "'"};
        }
    }

    if (explainVerbosity) {
        if (hasExplainElem) {
            return {
                ErrorCodes::FailedToParse,
                str::stream() << "The '" << kExplainName
                              << "' option is illegal when a explain verbosity is also provided"};
        }

        request.setExplain(explainVerbosity);
    }

    // 'hasExplainElem' implies an aggregate command-level explain option, which does not require
    // a cursor argument.
    if (!hasCursorElem && !hasExplainElem) {
        return {ErrorCodes::FailedToParse,
                str::stream()
                    << "The '"
                    << kCursorName
                    << "' option is required, except for aggregate with the explain argument"};
    }

    if (request.getExplain() && cmdObj[WriteConcernOptions::kWriteConcernField]) {
        return {ErrorCodes::FailedToParse,
                str::stream() << "Aggregation explain does not support the'"
                              << WriteConcernOptions::kWriteConcernField
                              << "' option"};
    }

    if (hasNeedsMergeElem && !hasFromMongersElem) {
        return {ErrorCodes::FailedToParse,
                str::stream() << "Cannot specify '" << kNeedsMergeName << "' without '"
                              << kFromMongersName
                              << "'"};
    }

    return request;
}

NamespaceString AggregationRequest::parseNs(const std::string& dbname, const BSONObj& cmdObj) {
    auto firstElement = cmdObj.firstElement();

    if (firstElement.isNumber()) {
        uassert(ErrorCodes::FailedToParse,
                str::stream() << "Invalid command format: the '"
                              << firstElement.fieldNameStringData()
                              << "' field must specify a collection name or 1",
                firstElement.number() == 1);
        return NamespaceString::makeCollectionlessAggregateNSS(dbname);
    } else {
        uassert(ErrorCodes::TypeMismatch,
                str::stream() << "collection name has invalid type: "
                              << typeName(firstElement.type()),
                firstElement.type() == BSONType::String);

        const NamespaceString nss(dbname, firstElement.valueStringData());

        uassert(ErrorCodes::InvalidNamespace,
                str::stream() << "Invalid namespace specified '" << nss.ns() << "'",
                nss.isValid() && !nss.isCollectionlessAggregateNS());

        return nss;
    }
}

Document AggregationRequest::serializeToCommandObj() const {
    MutableDocument serialized;
    return Document{
        {kCommandName, (_nss.isCollectionlessAggregateNS() ? Value(1) : Value(_nss.coll()))},
        {kPipelineName, _pipeline},
        // Only serialize booleans if different than their default.
        {kAllowDiskUseName, _allowDiskUse ? Value(true) : Value()},
        {kFromMongersName, _fromMongers ? Value(true) : Value()},
        {kNeedsMergeName, _needsMerge ? Value(true) : Value()},
        {bypassDocumentValidationCommandOption(),
         _bypassDocumentValidation ? Value(true) : Value()},
        // Only serialize a collation if one was specified.
        {kCollationName, _collation.isEmpty() ? Value() : Value(_collation)},
        // Only serialize batchSize if not an explain, otherwise serialize an empty cursor object.
        {kCursorName,
         _explainMode ? Value(Document()) : Value(Document{{kBatchSizeName, _batchSize}})},
        // Only serialize a hint if one was specified.
        {kHintName, _hint.isEmpty() ? Value() : Value(_hint)},
        // Only serialize a comment if one was specified.
        {kCommentName, _comment.empty() ? Value() : Value(_comment)},
        // Only serialize readConcern if specified.
        {repl::ReadConcernArgs::kReadConcernFieldName,
         _readConcern.isEmpty() ? Value() : Value(_readConcern)},
        // Only serialize the unwrapped read preference if specified.
        {QueryRequest::kUnwrappedReadPrefField,
         _unwrappedReadPref.isEmpty() ? Value() : Value(_unwrappedReadPref)},
        // Only serialize maxTimeMs if specified.
        {QueryRequest::cmdOptionMaxTimeMS,
         _maxTimeMS == 0 ? Value() : Value(static_cast<int>(_maxTimeMS))},
        {kExchangeName, _exchangeSpec ? Value(_exchangeSpec->toBSON()) : Value()},
        {WriteConcernOptions::kWriteConcernField,
         _writeConcern ? Value(_writeConcern->toBSON()) : Value()},
        // Only serialize runtime constants if any were specified.
        {kRuntimeConstants, _runtimeConstants ? Value(_runtimeConstants->toBSON()) : Value()},
    };
}
}  // namespace monger
