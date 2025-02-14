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

#include "monger/db/ftdc/ftdc_mongerd.h"

#include <boost/filesystem.hpp>

#include "monger/db/ftdc/constants.h"
#include "monger/db/ftdc/controller.h"
#include "monger/db/ftdc/ftdc_server.h"
#include "monger/db/repl/replication_coordinator.h"
#include "monger/db/storage/storage_options.h"

namespace monger {

namespace {
void registerMongerDCollectors(FTDCController* controller) {
    // These metrics are only collected if replication is enabled
    if (repl::ReplicationCoordinator::get(getGlobalServiceContext())->getReplicationMode() !=
        repl::ReplicationCoordinator::modeNone) {
        // CmdReplSetGetStatus
        controller->addPeriodicCollector(std::make_unique<FTDCSimpleInternalCommandCollector>(
            "replSetGetStatus", "replSetGetStatus", "", BSON("replSetGetStatus" << 1)));

        // CollectionStats
        controller->addPeriodicCollector(
            std::make_unique<FTDCSimpleInternalCommandCollector>("collStats",
                                                                 "local.oplog.rs.stats",
                                                                 "local",
                                                                 BSON("collStats"
                                                                      << "oplog.rs")));
    }
}

}  // namespace

void startMongerDFTDC() {
    auto dir = getFTDCDirectoryPathParameter();

    if (dir.empty()) {
        dir = storageGlobalParams.dbpath;
        dir /= kFTDCDefaultDirectory.toString();
    }

    startFTDC(dir, FTDCStartMode::kStart, registerMongerDCollectors);
}

void stopMongerDFTDC() {
    stopFTDC();
}

}  // namespace monger
