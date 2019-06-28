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
#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kFTDC

#include "monger/platform/basic.h"

#include "monger/db/ftdc/ftdc_mongers.h"

#include <boost/filesystem.hpp>

#include "monger/client/connpool.h"
#include "monger/client/dbclient_connection.h"
#include "monger/client/global_conn_pool.h"
#include "monger/db/ftdc/controller.h"
#include "monger/db/ftdc/ftdc_server.h"
#include "monger/db/repl/replication_coordinator.h"
#include "monger/executor/connection_pool_stats.h"
#include "monger/executor/task_executor_pool.h"
#include "monger/s/grid.h"
#include "monger/stdx/thread.h"
#include "monger/util/log.h"
#include "monger/util/synchronized_value.h"

namespace monger {

class ConnPoolStatsCollector : public FTDCCollectorInterface {
public:
    void collect(OperationContext* opCtx, BSONObjBuilder& builder) override {
        executor::ConnectionPoolStats stats{};

        // Global connection pool connections.
        globalConnPool.appendConnectionStats(&stats);

        // Sharding connections.
        {
            auto const grid = Grid::get(opCtx);
            if (grid->getExecutorPool()) {
                grid->getExecutorPool()->appendConnectionStats(&stats);
            }

            auto const customConnPoolStatsFn = grid->getCustomConnectionPoolStatsFn();
            if (customConnPoolStatsFn) {
                customConnPoolStatsFn(&stats);
            }
        }

        // Output to a BSON object.
        builder.appendNumber("numClientConnections", DBClientConnection::getNumConnections());
        builder.appendNumber("numAScopedConnections", AScopedConnection::getNumConnections());
        stats.appendToBSON(builder, true /* forFTDC */);

        // All replica sets being tracked.
        globalRSMonitorManager.report(&builder, true /* forFTDC */);
    }

    std::string name() const override {
        return "connPoolStats";
    }
};

void registerMongerSCollectors(FTDCController* controller) {
    // PoolStats
    controller->addPeriodicCollector(std::make_unique<ConnPoolStatsCollector>());
}

void startMongerSFTDC() {
    // Get the path to use for FTDC:
    // 1. Check if the user set one.
    // 2. If not, check if the user has a logpath and derive one.
    // 3. Otherwise, tell the user FTDC cannot run.

    // Only attempt to enable FTDC if we have a path to log files to.
    FTDCStartMode startMode = FTDCStartMode::kStart;
    auto directory = getFTDCDirectoryPathParameter();

    if (directory.empty()) {
        if (serverGlobalParams.logpath.empty()) {
            warning() << "FTDC is disabled because neither '--logpath' nor set parameter "
                         "'diagnosticDataCollectionDirectoryPath' are specified.";
            startMode = FTDCStartMode::kSkipStart;
        } else {
            directory = boost::filesystem::absolute(
                FTDCUtil::getMongerSPath(serverGlobalParams.logpath), serverGlobalParams.cwd);

            // Note: If the computed FTDC directory conflicts with an existing file, then FTDC will
            // warn about the conflict, and not startup. It will not terminate MongerS in this
            // situation.
        }
    }

    startFTDC(directory, startMode, registerMongerSCollectors);
}

void stopMongerSFTDC() {
    stopFTDC();
}

}  // namespace monger
