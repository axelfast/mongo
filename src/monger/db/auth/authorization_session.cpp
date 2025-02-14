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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kAccessControl

#include "monger/platform/basic.h"

#include "monger/db/auth/authorization_session.h"

#include <string>
#include <vector>

#include "monger/base/status.h"
#include "monger/db/auth/action_set.h"
#include "monger/db/auth/action_type.h"
#include "monger/db/auth/authz_session_external_state.h"
#include "monger/db/auth/privilege.h"
#include "monger/db/auth/restriction_environment.h"
#include "monger/db/auth/security_key.h"
#include "monger/db/auth/user_management_commands_parser.h"
#include "monger/db/bson/dotted_path_support.h"
#include "monger/db/catalog/document_validation.h"
#include "monger/db/client.h"
#include "monger/db/jsobj.h"
#include "monger/db/namespace_string.h"
#include "monger/db/pipeline/aggregation_request.h"
#include "monger/db/pipeline/lite_parsed_pipeline.h"
#include "monger/util/assert_util.h"
#include "monger/util/log.h"
#include "monger/util/str.h"

namespace monger {

AuthorizationSession::~AuthorizationSession() = default;

void AuthorizationSession::ScopedImpersonate::swap() {
    auto impersonations = _authSession._getImpersonations();
    using std::swap;
    swap(*std::get<0>(impersonations), _users);
    swap(*std::get<1>(impersonations), _roles);
}

MONGO_DEFINE_SHIM(AuthorizationSession::create);
}  // namespace monger
