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

#include "monger/s/request_types/add_shard_to_zone_request_type.h"

#include "monger/db/jsobj.h"
#include "monger/unittest/unittest.h"

namespace monger {

namespace {

TEST(AddShardToZoneRequest, BasicValidMongersCommand) {
    auto requestStatus = AddShardToZoneRequest::parseFromMongersCommand(BSON("addShardToZone"
                                                                            << "a"
                                                                            << "zone"
                                                                            << "z"));
    ASSERT_OK(requestStatus.getStatus());

    auto request = requestStatus.getValue();
    ASSERT_EQ("a", request.getShardName());
    ASSERT_EQ("z", request.getZoneName());
}

TEST(AddShardToZoneRequest, CommandBuilderShouldAlwaysCreateConfigCommand) {
    auto requestStatus = AddShardToZoneRequest::parseFromMongersCommand(BSON("addShardToZone"
                                                                            << "a"
                                                                            << "zone"
                                                                            << "z"));
    ASSERT_OK(requestStatus.getStatus());

    auto request = requestStatus.getValue();

    BSONObjBuilder builder;
    request.appendAsConfigCommand(&builder);
    auto cmdObj = builder.obj();

    ASSERT_BSONOBJ_EQ(BSON("_configsvrAddShardToZone"
                           << "a"
                           << "zone"
                           << "z"),
                      cmdObj);
}

TEST(AddShardToZoneRequest, MissingZoneErrors) {
    auto request = AddShardToZoneRequest::parseFromMongersCommand(BSON("addShardToZone"
                                                                      << "a"));
    ASSERT_EQ(ErrorCodes::NoSuchKey, request.getStatus());
}

TEST(AddShardToZoneRequest, MissingShardNameErrors) {
    auto request = AddShardToZoneRequest::parseFromMongersCommand(BSON("zone"
                                                                      << "z"));
    ASSERT_EQ(ErrorCodes::NoSuchKey, request.getStatus());
}

TEST(AddShardToZoneRequest, WrongShardNameTypeErrors) {
    auto request =
        AddShardToZoneRequest::parseFromMongersCommand(BSON("addShardToZone" << 1234 << "zone"
                                                                            << "z"));
    ASSERT_EQ(ErrorCodes::TypeMismatch, request.getStatus());
}

TEST(AddShardToZoneRequest, WrongZoneNameTypeErrors) {
    auto request = AddShardToZoneRequest::parseFromMongersCommand(BSON("addShardToZone"
                                                                      << "a"
                                                                      << "zone"
                                                                      << 1234));
    ASSERT_EQ(ErrorCodes::TypeMismatch, request.getStatus());
}

TEST(AddShardToZoneRequest, CannotUseMongersToParseConfigCommand) {
    auto request = AddShardToZoneRequest::parseFromMongersCommand(BSON("_configsvrAddShardToZone"
                                                                      << "a"
                                                                      << "zone"
                                                                      << "z"));
    ASSERT_EQ(ErrorCodes::NoSuchKey, request.getStatus());
}

TEST(CfgAddShardToZoneRequest, BasicValidConfigCommand) {
    auto requestStatus =
        AddShardToZoneRequest::parseFromConfigCommand(BSON("_configsvrAddShardToZone"
                                                           << "a"
                                                           << "zone"
                                                           << "z"));
    ASSERT_OK(requestStatus.getStatus());

    auto request = requestStatus.getValue();
    ASSERT_EQ("a", request.getShardName());
    ASSERT_EQ("z", request.getZoneName());

    BSONObjBuilder builder;
    request.appendAsConfigCommand(&builder);
    auto cmdObj = builder.obj();

    ASSERT_BSONOBJ_EQ(BSON("_configsvrAddShardToZone"
                           << "a"
                           << "zone"
                           << "z"),
                      cmdObj);
}

TEST(CfgAddShardToZoneRequest, MissingZoneErrors) {
    auto request = AddShardToZoneRequest::parseFromConfigCommand(BSON("_configsvrAddShardToZone"
                                                                      << "a"));
    ASSERT_EQ(ErrorCodes::NoSuchKey, request.getStatus());
}

TEST(CfgAddShardToZoneRequest, MissingShardNameErrors) {
    auto request = AddShardToZoneRequest::parseFromConfigCommand(BSON("zone"
                                                                      << "z"));
    ASSERT_EQ(ErrorCodes::NoSuchKey, request.getStatus());
}

TEST(CfgAddShardToZoneRequest, WrongShardNameTypeErrors) {
    auto request = AddShardToZoneRequest::parseFromConfigCommand(
        BSON("_configsvrAddShardToZone" << 1234 << "zone"
                                        << "z"));
    ASSERT_EQ(ErrorCodes::TypeMismatch, request.getStatus());
}

TEST(CfgAddShardToZoneRequest, WrongZoneNameTypeErrors) {
    auto request = AddShardToZoneRequest::parseFromConfigCommand(BSON("_configsvrAddShardToZone"
                                                                      << "a"
                                                                      << "zone"
                                                                      << 1234));
    ASSERT_EQ(ErrorCodes::TypeMismatch, request.getStatus());
}

TEST(CfgAddShardToZoneRequest, CannotUseConfigToParseMongersCommand) {
    auto request = AddShardToZoneRequest::parseFromConfigCommand(BSON("addShardToZone"
                                                                      << "a"
                                                                      << "zone"
                                                                      << 1234));
    ASSERT_EQ(ErrorCodes::NoSuchKey, request.getStatus());
}

}  // unnamed namespace
}  // namespace monger
