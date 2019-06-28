/**
 *    Copyright (C) 2018-present MongerDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongerDB, Inc.
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

#include "monger/base/init.h"
#include "monger/db/auth/action_type.h"
#include "monger/db/auth/authorization_session.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/ftdc/controller.h"
#include "monger/db/jsobj.h"
#include "monger/db/operation_context.h"

namespace monger {
namespace {

/**
 * getDiagnosticData is a MongerD only command. We implement in MongerS to give users a better error
 * message.
 */
class GetDiagnosticDataCommand final : public ErrmsgCommandDeprecated {
public:
    GetDiagnosticDataCommand() : ErrmsgCommandDeprecated("getDiagnosticData") {}

    bool adminOnly() const override {
        return true;
    }

    std::string help() const override {
        return "get latest diagnostic data collection snapshot";
    }

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override {
        return AllowedOnSecondary::kAlways;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }

    Status checkAuthForCommand(Client* client,
                               const std::string& dbname,
                               const BSONObj& cmdObj) const override {

        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forClusterResource(), ActionType::serverStatus)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }

        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forClusterResource(), ActionType::replSetGetStatus)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }

        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forClusterResource(), ActionType::connPoolStats)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }

        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forExactNamespace(NamespaceString("local", "oplog.rs")),
                ActionType::collStats)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }

        return Status::OK();
    }

    bool errmsgRun(OperationContext* opCtx,
                   const std::string& db,
                   const BSONObj& cmdObj,
                   std::string& errmsg,
                   BSONObjBuilder& result) override {

        result.append(
            "data",
            FTDCController::get(opCtx->getServiceContext())->getMostRecentPeriodicDocument());

        return true;
    }
};

Command* ftdcCommand;

MONGO_INITIALIZER(CreateDiagnosticDataCommand)(InitializerContext* context) {
    ftdcCommand = new GetDiagnosticDataCommand();

    return Status::OK();
}

}  // namespace
}  // namespace monger
