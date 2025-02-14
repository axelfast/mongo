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

#include "monger/db/commands/find_and_modify_common.h"

#include "monger/db/auth/action_set.h"
#include "monger/db/auth/action_type.h"
#include "monger/db/auth/resource_pattern.h"
#include "monger/db/catalog/document_validation.h"
#include "monger/db/commands.h"

namespace monger {
namespace find_and_modify {

void addPrivilegesRequiredForFindAndModify(const BasicCommand* commandTemplate,
                                           const std::string& dbname,
                                           const BSONObj& cmdObj,
                                           std::vector<Privilege>* out) {
    bool update = cmdObj["update"].trueValue();
    bool upsert = cmdObj["upsert"].trueValue();
    bool remove = cmdObj["remove"].trueValue();
    bool bypassDocumentValidation = shouldBypassDocumentValidationForCommand(cmdObj);

    ActionSet actions;
    actions.addAction(ActionType::find);
    if (update) {
        actions.addAction(ActionType::update);
    }
    if (upsert) {
        actions.addAction(ActionType::insert);
    }
    if (remove) {
        actions.addAction(ActionType::remove);
    }
    if (bypassDocumentValidation) {
        actions.addAction(ActionType::bypassDocumentValidation);
    }
    ResourcePattern resource(commandTemplate->parseResourcePattern(dbname, cmdObj));
    uassert(17137,
            "Invalid target namespace " + resource.toString(),
            resource.isExactNamespacePattern());
    out->push_back(Privilege(resource, actions));
}

}  // namespace find_and_modify
}  // namespace monger
