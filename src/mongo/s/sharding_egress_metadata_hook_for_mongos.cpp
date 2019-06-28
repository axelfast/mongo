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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kSharding

#include "monger/platform/basic.h"

#include "monger/s/sharding_egress_metadata_hook_for_mongers.h"

#include "monger/db/client.h"
#include "monger/rpc/metadata/sharding_metadata.h"
#include "monger/s/cluster_last_error_info.h"
#include "monger/s/grid.h"
#include "monger/util/log.h"

namespace monger {
namespace rpc {

void ShardingEgressMetadataHookForMongers::_saveGLEStats(const BSONObj& metadata,
                                                        StringData hostString) {}

repl::OpTime ShardingEgressMetadataHookForMongers::_getConfigServerOpTime() {
    return Grid::get(_serviceContext)->configOpTime();
}

}  // namespace rpc
}  // namespace monger
