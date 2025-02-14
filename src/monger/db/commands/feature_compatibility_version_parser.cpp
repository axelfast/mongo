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

#include "monger/db/commands/feature_compatibility_version_parser.h"

#include "monger/base/status.h"
#include "monger/bson/bsonobj.h"
#include "monger/db/commands/feature_compatibility_version_documentation.h"
#include "monger/db/namespace_string.h"

namespace monger {

constexpr StringData FeatureCompatibilityVersionParser::kVersion40;
constexpr StringData FeatureCompatibilityVersionParser::kVersion42;
constexpr StringData FeatureCompatibilityVersionParser::kVersionDowngradingTo40;
constexpr StringData FeatureCompatibilityVersionParser::kVersionUpgradingTo42;
constexpr StringData FeatureCompatibilityVersionParser::kVersionUnset;

constexpr StringData FeatureCompatibilityVersionParser::kParameterName;
constexpr StringData FeatureCompatibilityVersionParser::kVersionField;
constexpr StringData FeatureCompatibilityVersionParser::kTargetVersionField;

StatusWith<ServerGlobalParams::FeatureCompatibility::Version>
FeatureCompatibilityVersionParser::parse(const BSONObj& featureCompatibilityVersionDoc) {
    ServerGlobalParams::FeatureCompatibility::Version version =
        ServerGlobalParams::FeatureCompatibility::Version::kUnsetDefault40Behavior;
    std::string versionString;
    std::string targetVersionString;

    for (auto&& elem : featureCompatibilityVersionDoc) {
        auto fieldName = elem.fieldNameStringData();
        if (fieldName == "_id") {
            continue;
        } else if (fieldName == kVersionField || fieldName == kTargetVersionField) {
            if (elem.type() != BSONType::String) {
                return Status(
                    ErrorCodes::TypeMismatch,
                    str::stream() << fieldName << " must be of type String, but was of type "
                                  << typeName(elem.type())
                                  << ". Contents of "
                                  << kParameterName
                                  << " document in "
                                  << NamespaceString::kServerConfigurationNamespace.toString()
                                  << ": "
                                  << featureCompatibilityVersionDoc
                                  << ". See "
                                  << feature_compatibility_version_documentation::kCompatibilityLink
                                  << ".");
            }

            if (elem.String() != kVersion42 && elem.String() != kVersion40) {
                return Status(
                    ErrorCodes::BadValue,
                    str::stream() << "Invalid value for " << fieldName << ", found "
                                  << elem.String()
                                  << ", expected '"
                                  << kVersion42
                                  << "' or '"
                                  << kVersion40
                                  << "'. Contents of "
                                  << kParameterName
                                  << " document in "
                                  << NamespaceString::kServerConfigurationNamespace.toString()
                                  << ": "
                                  << featureCompatibilityVersionDoc
                                  << ". See "
                                  << feature_compatibility_version_documentation::kCompatibilityLink
                                  << ".");
            }

            if (fieldName == kVersionField) {
                versionString = elem.String();
            } else if (fieldName == kTargetVersionField) {
                targetVersionString = elem.String();
            }
        } else {
            return Status(
                ErrorCodes::BadValue,
                str::stream() << "Unrecognized field '" << fieldName << "'. Contents of "
                              << kParameterName
                              << " document in "
                              << NamespaceString::kServerConfigurationNamespace.toString()
                              << ": "
                              << featureCompatibilityVersionDoc
                              << ". See "
                              << feature_compatibility_version_documentation::kCompatibilityLink
                              << ".");
        }
    }

    if (versionString == kVersion40) {
        if (targetVersionString == kVersion42) {
            version = ServerGlobalParams::FeatureCompatibility::Version::kUpgradingTo42;
        } else if (targetVersionString == kVersion40) {
            version = ServerGlobalParams::FeatureCompatibility::Version::kDowngradingTo40;
        } else {
            version = ServerGlobalParams::FeatureCompatibility::Version::kFullyDowngradedTo40;
        }
    } else if (versionString == kVersion42) {
        if (targetVersionString == kVersion42 || targetVersionString == kVersion40) {
            return Status(
                ErrorCodes::BadValue,
                str::stream() << "Invalid state for " << kParameterName << " document in "
                              << NamespaceString::kServerConfigurationNamespace.toString()
                              << ": "
                              << featureCompatibilityVersionDoc
                              << ". See "
                              << feature_compatibility_version_documentation::kCompatibilityLink
                              << ".");
        } else {
            version = ServerGlobalParams::FeatureCompatibility::Version::kFullyUpgradedTo42;
        }
    } else {
        return Status(
            ErrorCodes::BadValue,
            str::stream() << "Missing required field '" << kVersionField << "''. Contents of "
                          << kParameterName
                          << " document in "
                          << NamespaceString::kServerConfigurationNamespace.toString()
                          << ": "
                          << featureCompatibilityVersionDoc
                          << ". See "
                          << feature_compatibility_version_documentation::kCompatibilityLink
                          << ".");
    }

    return version;
}

}  // namespace monger
