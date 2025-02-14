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

#include "monger/rpc/protocol.h"

#include <algorithm>
#include <iterator>

#include "monger/base/string_data.h"
#include "monger/bson/util/bson_extract.h"
#include "monger/db/jsobj.h"
#include "monger/db/wire_version.h"
#include "monger/util/str.h"

namespace monger {
namespace rpc {

namespace {

/**
 * Protocols supported by order of preference.
 */
const Protocol kPreferredProtos[] = {Protocol::kOpMsg, Protocol::kOpQuery};

struct ProtocolSetAndName {
    StringData name;
    ProtocolSet protocols;
};

constexpr ProtocolSetAndName protocolSetNames[] = {
    // Most common ones go first.
    {"all"_sd, supports::kAll},                  // new mongerd and mongers or very new client.
    {"opQueryOnly"_sd, supports::kOpQueryOnly},  // old mongers or mongerd or moderately old client.

    // Then the rest (these should never happen in production).
    {"none"_sd, supports::kNone},
    {"opMsgOnly"_sd, supports::kOpMsgOnly},
};

}  // namespace

Protocol protocolForMessage(const Message& message) {
    switch (message.operation()) {
        case monger::dbMsg:
            return Protocol::kOpMsg;
        case monger::dbQuery:
            return Protocol::kOpQuery;
        default:
            uasserted(ErrorCodes::UnsupportedFormat,
                      str::stream() << "Received a reply message with unexpected opcode: "
                                    << message.operation());
    }
}

StatusWith<Protocol> negotiate(ProtocolSet fst, ProtocolSet snd) {
    using std::begin;
    using std::end;

    ProtocolSet common = fst & snd;

    auto it = std::find_if(begin(kPreferredProtos), end(kPreferredProtos), [common](Protocol p) {
        return common & static_cast<ProtocolSet>(p);
    });

    if (it == end(kPreferredProtos)) {
        return Status(ErrorCodes::RPCProtocolNegotiationFailed, "No common protocol found.");
    }
    return *it;
}

StatusWith<StringData> toString(ProtocolSet protocols) {
    for (auto& elem : protocolSetNames) {
        if (elem.protocols == protocols)
            return elem.name;
    }
    return Status(ErrorCodes::BadValue,
                  str::stream() << "ProtocolSet " << protocols
                                << " does not match any well-known value.");
}

StatusWith<ProtocolSet> parseProtocolSet(StringData name) {
    for (auto& elem : protocolSetNames) {
        if (elem.name == name)
            return elem.protocols;
    }
    return Status(ErrorCodes::BadValue,
                  str::stream() << name << " is not a valid name for a ProtocolSet.");
}

StatusWith<ProtocolSetAndWireVersionInfo> parseProtocolSetFromIsMasterReply(
    const BSONObj& isMasterReply) {
    long long maxWireVersion;
    auto maxWireExtractStatus =
        bsonExtractIntegerField(isMasterReply, "maxWireVersion", &maxWireVersion);

    long long minWireVersion;
    auto minWireExtractStatus =
        bsonExtractIntegerField(isMasterReply, "minWireVersion", &minWireVersion);

    // MongerDB 2.4 and earlier do not have maxWireVersion/minWireVersion in their 'isMaster' replies
    if ((maxWireExtractStatus == minWireExtractStatus) &&
        (maxWireExtractStatus == ErrorCodes::NoSuchKey)) {
        return {{supports::kOpQueryOnly, {0, 0}}};
    } else if (!maxWireExtractStatus.isOK()) {
        return maxWireExtractStatus;
    } else if (!minWireExtractStatus.isOK()) {
        return minWireExtractStatus;
    }

    if (minWireVersion < 0 || maxWireVersion < 0 ||
        minWireVersion >= std::numeric_limits<int>::max() ||
        maxWireVersion >= std::numeric_limits<int>::max()) {
        return Status(ErrorCodes::IncompatibleServerVersion,
                      str::stream() << "Server min and max wire version have invalid values ("
                                    << minWireVersion
                                    << ","
                                    << maxWireVersion
                                    << ")");
    }

    WireVersionInfo version{static_cast<int>(minWireVersion), static_cast<int>(maxWireVersion)};

    auto protos = computeProtocolSet(version);
    return {{protos, version}};
}

ProtocolSet computeProtocolSet(const WireVersionInfo version) {
    ProtocolSet result = supports::kNone;
    if (version.minWireVersion <= version.maxWireVersion) {
        if (version.maxWireVersion >= WireVersion::SUPPORTS_OP_MSG) {
            result |= supports::kOpMsgOnly;
        }
        if (version.minWireVersion <= WireVersion::RELEASE_2_4_AND_BEFORE) {
            result |= supports::kOpQueryOnly;
        }
        // Note: this means anything using the internal handshake cannot talk to servers between 2.6
        // and 3.6, since the servers will reply with higher minWireVersions. The shell should still
        // be able to connect to those versions but will just use OP_QUERY to run commands.
    }
    return result;
}

Status validateWireVersion(const WireVersionInfo client, const WireVersionInfo server) {
    // Since this is defined in the code, it should always hold true since this is the versions that
    // mongers/d wants to connect to.
    invariant(client.minWireVersion <= client.maxWireVersion);

    // Server may return bad data.
    if (server.minWireVersion > server.maxWireVersion) {
        return Status(ErrorCodes::IncompatibleServerVersion,
                      str::stream() << "Server min and max wire version are incorrect ("
                                    << server.minWireVersion
                                    << ","
                                    << server.maxWireVersion
                                    << ")");
    }

    // Determine if the [min, max] tuples overlap.
    // We assert the invariant that min < max above.
    if (!(client.minWireVersion <= server.maxWireVersion &&
          client.maxWireVersion >= server.minWireVersion)) {
        std::string errmsg = str::stream()
            << "Server min and max wire version (" << server.minWireVersion << ","
            << server.maxWireVersion << ") is incompatible with client min wire version ("
            << client.minWireVersion << "," << client.maxWireVersion << ").";
        if (client.maxWireVersion < server.minWireVersion) {
            return Status(ErrorCodes::IncompatibleWithUpgradedServer,
                          str::stream()
                              << errmsg
                              << "You (client) are attempting to connect to a node (server) that "
                                 "no longer accepts connections with your (client’s) binary "
                                 "version. Please upgrade the client’s binary version.");
        }
        return Status(ErrorCodes::IncompatibleServerVersion,
                      str::stream() << errmsg
                                    << "You (client) are attempting to connect to a node "
                                       "(server) with a binary version with which "
                                       "you (client) no longer accept connections. Please "
                                       "upgrade the server’s binary version.");
    }

    return Status::OK();
}

}  // namespace rpc
}  // namespace monger
