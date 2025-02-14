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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kSharding

#include "monger/platform/basic.h"

#include "monger/db/s/sharded_connection_info.h"

#include "monger/db/client.h"
#include "monger/util/log.h"

namespace monger {
namespace {

const auto clientSCI = Client::declareDecoration<boost::optional<ShardedConnectionInfo>>();

}  // namespace

ShardedConnectionInfo::ShardedConnectionInfo() = default;

ShardedConnectionInfo::~ShardedConnectionInfo() = default;

ShardedConnectionInfo* ShardedConnectionInfo::get(Client* client, bool create) {
    auto& current = clientSCI(client);

    if (!current && create) {
        LOG(1) << "entering shard mode for connection";
        current.emplace();
    }

    return current ? &current.value() : nullptr;
}

void ShardedConnectionInfo::reset(Client* client) {
    clientSCI(client) = boost::none;
}

boost::optional<ChunkVersion> ShardedConnectionInfo::getVersion(const std::string& ns) const {
    NSVersionMap::const_iterator it = _versions.find(ns);
    if (it != _versions.end()) {
        return it->second;
    } else {
        return boost::none;
    }
}

void ShardedConnectionInfo::setVersion(const std::string& ns, const ChunkVersion& version) {
    _versions[ns] = version;
}

}  // namespace monger
