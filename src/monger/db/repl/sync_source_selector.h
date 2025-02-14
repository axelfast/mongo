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

#include "monger/rpc/metadata/oplog_query_metadata.h"
#include "monger/util/net/hostandport.h"
#include "monger/util/time_support.h"

namespace monger {

class OperationContext;
class Timestamp;

namespace rpc {
class ReplSetMetadata;
class OplogQueryMetadata;
}

namespace repl {

class OpTime;
struct SyncSourceResolverResponse;

/**
 * Manage list of viable and blocked sync sources that we can replicate from.
 */
class SyncSourceSelector {
    SyncSourceSelector(const SyncSourceSelector&) = delete;
    SyncSourceSelector& operator=(const SyncSourceSelector&) = delete;

public:
    SyncSourceSelector() = default;
    virtual ~SyncSourceSelector() = default;

    /**
     * Clears the list of sync sources we have blacklisted.
     */
    virtual void clearSyncSourceBlacklist() = 0;

    /**
     * Chooses a viable sync source, or, if none available, returns empty HostAndPort.
     */
    virtual HostAndPort chooseNewSyncSource(const OpTime& lastOpTimeFetched) = 0;

    /**
     * Blacklists choosing 'host' as a sync source until time 'until'.
     */
    virtual void blacklistSyncSource(const HostAndPort& host, Date_t until) = 0;

    /**
     * Determines if a new sync source should be chosen, if a better candidate sync source is
     * available.  If the current sync source's last optime (visibleOpTime or appliedOpTime of
     * metadata under protocolVersion 1, but pulled from the MemberHeartbeatData in protocolVersion
     * 0) is more than _maxSyncSourceLagSecs behind any syncable source, this function returns true.
     * If we are running in ProtocolVersion 1, our current sync source is not primary, has no sync
     * source and only has data up to "myLastOpTime", returns true.
     *
     * "now" is used to skip over currently blacklisted sync sources.
     *
     * OplogQueryMetadata is optional for compatibility with 3.4 servers that do not know to
     * send OplogQueryMetadata.
     * TODO (SERVER-27668): Make OplogQueryMetadata non-optional in mongerdb 3.8.
     */
    virtual bool shouldChangeSyncSource(const HostAndPort& currentSource,
                                        const rpc::ReplSetMetadata& replMetadata,
                                        boost::optional<rpc::OplogQueryMetadata> oqMetadata) = 0;
};

}  // namespace repl
}  // namespace monger
