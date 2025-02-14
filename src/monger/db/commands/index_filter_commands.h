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

#pragma once

#include "monger/db/commands.h"
#include "monger/db/query/plan_cache.h"
#include "monger/db/query/query_settings.h"

namespace monger {

/**
 * DB commands for index filters.
 * Index filter commands work on a different data structure in the collection
 * info cache from the plan cache.
 * The user still thinks of index filter commands as part of the plan cache functionality
 * so the command name prefix is still "planCache".
 *
 * These are in a header to facilitate unit testing. See index_filter_commands_test.cpp.
 */

/**
 * IndexFilterCommand
 * Defines common attributes for all index filter related commands
 * such as slaveOk.
 */
class IndexFilterCommand : public BasicCommand {
public:
    IndexFilterCommand(const std::string& name, const std::string& helpText);

    /**
     * Entry point from command subsystem.
     * Implementation provides standardization of error handling
     * such as adding error code and message to BSON result.
     *
     * Do not override in derived classes.
     * Override runPlanCacheCommands instead to
     * implement plan cache command functionality.
     */

    bool run(OperationContext* opCtx,
             const std::string& dbname,
             const BSONObj& cmdObj,
             BSONObjBuilder& result);

    virtual bool supportsWriteConcern(const BSONObj& cmd) const override;

    AllowedOnSecondary secondaryAllowed(ServiceContext*) const override;

    std::string help() const override;

    /**
     * One action type defined for index filter commands:
     * - planCacheIndexFilter
     */
    virtual Status checkAuthForCommand(Client* client,
                                       const std::string& dbname,
                                       const BSONObj& cmdObj) const;

    /**
     * Subset of command arguments used by index filter commands
     * Override to provide command functionality.
     * Should contain just enough logic to invoke run*Command() function
     * in query_settings.h
     */
    virtual Status runIndexFilterCommand(OperationContext* opCtx,
                                         const std::string& ns,
                                         const BSONObj& cmdObj,
                                         BSONObjBuilder* bob) = 0;

private:
    std::string helpText;
};

/**
 * ListFilters
 *
 * { planCacheListFilters: <collection> }
 *
 */
class ListFilters : public IndexFilterCommand {
public:
    ListFilters();

    virtual Status runIndexFilterCommand(OperationContext* opCtx,
                                         const std::string& ns,
                                         const BSONObj& cmdObj,
                                         BSONObjBuilder* bob);

    /**
     * Looks up index filters from collection's query settings.
     * Inserts index filters into BSON builder.
     */
    static Status list(const QuerySettings& querySettings, BSONObjBuilder* bob);
};

/**
 * ClearFilters
 *
 * { planCacheClearFilters: <collection>, query: <query>, sort: <sort>, projection: <projection> }
 *
 */
class ClearFilters : public IndexFilterCommand {
public:
    ClearFilters();

    virtual Status runIndexFilterCommand(OperationContext* opCtx,
                                         const std::string& ns,
                                         const BSONObj& cmdObj,
                                         BSONObjBuilder* bob);

    /**
     * If query shape is provided, clears index filter for a query.
     * Otherwise, clears collection's filters.
     * Namespace argument ns is ignored if we are clearing the entire cache.
     * Removes corresponding entries from plan cache.
     */
    static Status clear(OperationContext* opCtx,
                        QuerySettings* querySettings,
                        PlanCache* planCache,
                        const std::string& ns,
                        const BSONObj& cmdObj);
};

/**
 * SetFilter
 *
 * {
 *     planCacheSetFilter: <collection>,
 *     query: <query>,
 *     sort: <sort>,
 *     projection: <projection>,
 *     indexes: [ <index1>, <index2>, <index3>, ... ]
 * }
 *
 */
class SetFilter : public IndexFilterCommand {
public:
    SetFilter();

    virtual Status runIndexFilterCommand(OperationContext* opCtx,
                                         const std::string& ns,
                                         const BSONObj& cmdObj,
                                         BSONObjBuilder* bob);

    /**
     * Sets index filter for a query shape.
     * Removes entry for query shape from plan cache.
     */
    static Status set(OperationContext* opCtx,
                      QuerySettings* querySettings,
                      PlanCache* planCache,
                      const std::string& ns,
                      const BSONObj& cmdObj);
};

}  // namespace monger
