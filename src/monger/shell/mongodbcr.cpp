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

#include "monger/client/authenticate.h"

#include "monger/base/init.h"
#include "monger/base/status.h"
#include "monger/base/status_with.h"
#include "monger/base/string_data.h"
#include "monger/bson/util/bson_extract.h"
#include "monger/db/auth/sasl_command_constants.h"
#include "monger/rpc/get_status_from_command_result.h"
#include "monger/rpc/op_msg.h"
#include "monger/rpc/unique_message.h"
#include "monger/util/password_digest.h"

namespace monger {
namespace auth {
namespace {

const StringData kUserSourceFieldName = "userSource"_sd;
const BSONObj kGetNonceCmd = BSON("getnonce" << 1);

StatusWith<std::string> extractDBField(const BSONObj& params) {
    std::string db;
    if (params.hasField(kUserSourceFieldName)) {
        if (!bsonExtractStringField(params, kUserSourceFieldName, &db).isOK()) {
            return {ErrorCodes::AuthenticationFailed, "userSource field must contain a string"};
        }
    } else {
        if (!bsonExtractStringField(params, saslCommandUserDBFieldName, &db).isOK()) {
            return {ErrorCodes::AuthenticationFailed, "db field must contain a string"};
        }
    }

    return std::move(db);
}

StatusWith<OpMsgRequest> createMongerCRGetNonceCmd(const BSONObj& params) {
    auto db = extractDBField(params);
    if (!db.isOK()) {
        return std::move(db.getStatus());
    }

    return OpMsgRequest::fromDBAndBody(db.getValue(), kGetNonceCmd);
}

OpMsgRequest createMongerCRAuthenticateCmd(const BSONObj& params, StringData nonce) {
    std::string username;
    uassertStatusOK(bsonExtractStringField(params, saslCommandUserFieldName, &username));

    std::string password;
    uassertStatusOK(bsonExtractStringField(params, saslCommandPasswordFieldName, &password));

    bool shouldDigest;
    uassertStatusOK(bsonExtractBooleanFieldWithDefault(
        params, saslCommandDigestPasswordFieldName, true, &shouldDigest));

    std::string digested = password;
    if (shouldDigest) {
        digested = createPasswordDigest(username, password);
    }

    BSONObjBuilder b;
    {
        b << "authenticate" << 1 << "nonce" << nonce << "user" << username;
        md5digest d;
        {
            md5_state_t st;
            md5_init(&st);
            md5_append(&st, reinterpret_cast<const md5_byte_t*>(nonce.rawData()), nonce.size());
            md5_append(&st, reinterpret_cast<const md5_byte_t*>(username.c_str()), username.size());
            md5_append(&st, reinterpret_cast<const md5_byte_t*>(digested.c_str()), digested.size());
            md5_finish(&st, d);
        }
        b << "key" << digestToString(d);
    }

    return OpMsgRequest::fromDBAndBody(uassertStatusOK(extractDBField(params)), b.obj());
}

Future<void> authMongerCRImpl(RunCommandHook runCommand, const BSONObj& params) {
    invariant(runCommand);

    // Step 1: send getnonce command, receive nonce
    auto nonceRequest = createMongerCRGetNonceCmd(params);
    if (!nonceRequest.isOK()) {
        return nonceRequest.getStatus();
    }

    return runCommand(nonceRequest.getValue())
        .then([runCommand, params](BSONObj nonceResponse) -> Future<void> {
            auto status = getStatusFromCommandResult(nonceResponse);
            if (!status.isOK()) {
                return status;
            }

            // Ensure response was valid
            std::string nonce;
            auto valid = bsonExtractStringField(nonceResponse, "nonce", &nonce);
            if (!valid.isOK())
                return Status(ErrorCodes::AuthenticationFailed,
                              "Invalid nonce response: " + nonceResponse.toString());

            return runCommand(createMongerCRAuthenticateCmd(params, nonce)).then([](BSONObj reply) {
                return getStatusFromCommandResult(reply);
            });
        });
}

MONGO_INITIALIZER(RegisterAuthMongerCR)(InitializerContext* context) {
    authMongerCR = authMongerCRImpl;
    return Status::OK();
}

}  // namespace
}  // namespace auth
}  // namespace monger
