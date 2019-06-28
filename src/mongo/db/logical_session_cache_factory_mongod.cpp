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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kControl

#include "monger/platform/basic.h"

#include "monger/db/logical_session_cache_factory_mongerd.h"

#include <memory>

#include "monger/db/logical_session_cache_impl.h"
#include "monger/db/service_liaison_mongerd.h"
#include "monger/db/session_catalog_mongerd.h"
#include "monger/db/sessions_collection_config_server.h"
#include "monger/db/sessions_collection_rs.h"
#include "monger/db/sessions_collection_sharded.h"
#include "monger/db/sessions_collection_standalone.h"
#include "monger/util/log.h"

namespace monger {

std::unique_ptr<LogicalSessionCache> makeLogicalSessionCacheD(LogicalSessionCacheServer state) {
    auto liaison = std::make_unique<ServiceLiaisonMongod>();

    auto sessionsColl = [&]() -> std::shared_ptr<SessionsCollection> {
        switch (state) {
            case LogicalSessionCacheServer::kSharded:
                return std::make_shared<SessionsCollectionSharded>();
            case LogicalSessionCacheServer::kConfigServer:
                return std::make_shared<SessionsCollectionConfigServer>();
            case LogicalSessionCacheServer::kReplicaSet:
                return std::make_shared<SessionsCollectionRS>();
            case LogicalSessionCacheServer::kStandalone:
                return std::make_shared<SessionsCollectionStandalone>();
        }

        MONGO_UNREACHABLE;
    }();

    return std::make_unique<LogicalSessionCacheImpl>(
        std::move(liaison), std::move(sessionsColl), MongoDSessionCatalog::reapSessionsOlderThan);
}

}  // namespace monger
