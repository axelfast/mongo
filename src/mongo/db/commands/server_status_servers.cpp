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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kCommand

#include "monger/platform/basic.h"

#include "monger/config.h"
#include "monger/db/commands/server_status.h"
#include "monger/transport/message_compressor_registry.h"
#include "monger/transport/service_entry_point.h"
#include "monger/util/net/hostname_canonicalization.h"
#include "monger/util/net/socket_utils.h"
#include "monger/util/net/ssl_manager.h"

namespace monger {

using std::endl;
using std::map;
using std::string;
using std::stringstream;

namespace {

// some universal sections

class Connections : public ServerStatusSection {
public:
    Connections() : ServerStatusSection("connections") {}

    bool includeByDefault() const override {
        return true;
    }

    BSONObj generateSection(OperationContext* opCtx,
                            const BSONElement& configElement) const override {
        BSONObjBuilder bb;

        auto serviceEntryPoint = opCtx->getServiceContext()->getServiceEntryPoint();
        invariant(serviceEntryPoint);

        serviceEntryPoint->appendStats(&bb);
        return bb.obj();
    }

} connections;

class Network : public ServerStatusSection {
public:
    Network() : ServerStatusSection("network") {}

    bool includeByDefault() const override {
        return true;
    }

    BSONObj generateSection(OperationContext* opCtx,
                            const BSONElement& configElement) const override {
        BSONObjBuilder b;
        networkCounter.append(b);
        appendMessageCompressionStats(&b);
        auto executor = opCtx->getServiceContext()->getServiceExecutor();
        if (executor) {
            BSONObjBuilder section(b.subobjStart("serviceExecutorTaskStats"));
            executor->appendStats(&section);
        }

        return b.obj();
    }

} network;

#ifdef MONGO_CONFIG_SSL
class Security : public ServerStatusSection {
public:
    Security() : ServerStatusSection("security") {}

    bool includeByDefault() const override {
        return true;
    }

    BSONObj generateSection(OperationContext* opCtx,
                            const BSONElement& configElement) const override {
        BSONObj result;
        if (getSSLManager()) {
            result = getSSLManager()->getSSLConfiguration().getServerStatusBSON();
        }

        return result;
    }
} security;
#endif

class AdvisoryHostFQDNs final : public ServerStatusSection {
public:
    AdvisoryHostFQDNs() : ServerStatusSection("advisoryHostFQDNs") {}

    bool includeByDefault() const override {
        return false;
    }

    void appendSection(OperationContext* opCtx,
                       const BSONElement& configElement,
                       BSONObjBuilder* out) const override {
        out->append(
            "advisoryHostFQDNs",
            getHostFQDNs(getHostNameCached(), HostnameCanonicalizationMode::kForwardAndReverse));
    }
} advisoryHostFQDNs;
}  // namespace

}  // namespace monger
