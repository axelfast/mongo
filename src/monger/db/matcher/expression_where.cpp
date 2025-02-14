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

#include "monger/db/matcher/expression_where.h"

#include <memory>

#include "monger/base/init.h"
#include "monger/db/auth/authorization_session.h"
#include "monger/db/client.h"
#include "monger/db/jsobj.h"
#include "monger/db/matcher/expression.h"
#include "monger/db/matcher/expression_parser.h"
#include "monger/db/namespace_string.h"
#include "monger/scripting/engine.h"
#include "monger/util/scopeguard.h"


namespace monger {

using std::unique_ptr;
using std::string;
using std::stringstream;

WhereMatchExpression::WhereMatchExpression(OperationContext* opCtx,
                                           WhereParams params,
                                           StringData dbName)
    : WhereMatchExpressionBase(std::move(params)), _dbName(dbName.toString()), _opCtx(opCtx) {
    invariant(_opCtx != nullptr);

    uassert(
        ErrorCodes::BadValue, "no globalScriptEngine in $where parsing", getGlobalScriptEngine());

    uassert(ErrorCodes::BadValue, "ns for $where cannot be empty", dbName.size() != 0);

    const string userToken =
        AuthorizationSession::get(Client::getCurrent())->getAuthenticatedUserNamesToken();

    _scope = getGlobalScriptEngine()->getPooledScope(_opCtx, _dbName, "where" + userToken);
    const auto guard = makeGuard([&] { _scope->unregisterOperation(); });

    _func = _scope->createFunction(getCode().c_str());

    uassert(ErrorCodes::BadValue, "$where compile error", _func);
}

bool WhereMatchExpression::matches(const MatchableDocument* doc, MatchDetails* details) const {
    uassert(28692, "$where compile error", _func);
    BSONObj obj = doc->toBSON();

    _scope->registerOperation(Client::getCurrent()->getOperationContext());
    const auto guard = makeGuard([&] { _scope->unregisterOperation(); });

    if (!getScope().isEmpty()) {
        _scope->init(&getScope());
    }

    _scope->advanceGeneration();
    _scope->setObject("obj", const_cast<BSONObj&>(obj));
    _scope->setBoolean("fullObject", true);  // this is a hack b/c fullObject used to be relevant

    int err = _scope->invoke(_func, nullptr, &obj, 1000 * 60, false);
    if (err == -3) {  // INVOKE_ERROR
        stringstream ss;
        ss << "error on invocation of $where function:\n" << _scope->getError();
        uassert(16812, ss.str(), false);
    } else if (err != 0) {  // ! INVOKE_SUCCESS
        uassert(16813, "unknown error in invocation of $where function", false);
    }

    return _scope->getBoolean("__returnValue") != 0;
}

unique_ptr<MatchExpression> WhereMatchExpression::shallowClone() const {
    WhereParams params;
    params.code = getCode();
    params.scope = getScope();
    unique_ptr<WhereMatchExpression> e =
        std::make_unique<WhereMatchExpression>(_opCtx, std::move(params), _dbName);
    if (getTag()) {
        e->setTag(getTag()->clone());
    }
    return std::move(e);
}
}
