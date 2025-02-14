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

#include "monger/base/status.h"
#include "monger/db/jsobj.h"
#include "monger/db/wire_version.h"
#include "monger/rpc/protocol.h"
#include "monger/unittest/unittest.h"

namespace {

using monger::WireVersion;
using namespace monger::rpc;
using monger::unittest::assertGet;
using monger::BSONObj;

// Checks if negotiation of the first to protocol sets results in the 'proto'
const auto assert_negotiated = [](ProtocolSet fst, ProtocolSet snd, Protocol proto) {
    auto negotiated = negotiate(fst, snd);
    ASSERT_TRUE(negotiated.isOK());
    ASSERT_TRUE(negotiated.getValue() == proto);
};

TEST(Protocol, SuccessfulNegotiation) {
    assert_negotiated(supports::kAll, supports::kAll, Protocol::kOpMsg);
    assert_negotiated(supports::kAll, supports::kOpMsgOnly, Protocol::kOpMsg);
    assert_negotiated(supports::kAll, supports::kOpQueryOnly, Protocol::kOpQuery);
}

// Checks that negotiation fails
const auto assert_not_negotiated = [](ProtocolSet fst, ProtocolSet snd) {
    auto proto = negotiate(fst, snd);
    ASSERT_TRUE(!proto.isOK());
    ASSERT_TRUE(proto.getStatus().code() == monger::ErrorCodes::RPCProtocolNegotiationFailed);
};

TEST(Protocol, FailedNegotiation) {
    assert_not_negotiated(supports::kOpQueryOnly, supports::kOpMsgOnly);
    assert_not_negotiated(supports::kAll, supports::kNone);
    assert_not_negotiated(supports::kOpQueryOnly, supports::kNone);
    assert_not_negotiated(supports::kOpMsgOnly, supports::kNone);
}

TEST(Protocol, parseProtocolSetFromIsMasterReply) {
    {
        // MongerDB 4.0
        auto mongerd40 =
            BSON("maxWireVersion" << static_cast<int>(WireVersion::REPLICA_SET_TRANSACTIONS)
                                  << "minWireVersion"
                                  << static_cast<int>(WireVersion::RELEASE_2_4_AND_BEFORE));

        ASSERT_EQ(assertGet(parseProtocolSetFromIsMasterReply(mongerd40)).protocolSet,
                  supports::kAll);
    }
    {
        // MongerDB 3.6
        auto mongerd36 =
            BSON("maxWireVersion" << static_cast<int>(WireVersion::SUPPORTS_OP_MSG)  //
                                  << "minWireVersion"
                                  << static_cast<int>(WireVersion::RELEASE_2_4_AND_BEFORE));

        ASSERT_EQ(assertGet(parseProtocolSetFromIsMasterReply(mongerd36)).protocolSet,
                  supports::kAll);
    }
    {
        // MongerDB 3.2 (mongerd)
        auto mongerd32 =
            BSON("maxWireVersion" << static_cast<int>(WireVersion::COMMANDS_ACCEPT_WRITE_CONCERN)
                                  << "minWireVersion"
                                  << static_cast<int>(WireVersion::RELEASE_2_4_AND_BEFORE));

        ASSERT_EQ(assertGet(parseProtocolSetFromIsMasterReply(mongerd32)).protocolSet,
                  supports::kOpQueryOnly);  // This used to also include OP_COMMAND.
    }
    {
        // MongerDB 3.2 (mongers)
        auto mongers32 =
            BSON("maxWireVersion" << static_cast<int>(WireVersion::COMMANDS_ACCEPT_WRITE_CONCERN)
                                  << "minWireVersion"
                                  << static_cast<int>(WireVersion::RELEASE_2_4_AND_BEFORE)
                                  << "msg"
                                  << "isdbgrid");

        ASSERT_EQ(assertGet(parseProtocolSetFromIsMasterReply(mongers32)).protocolSet,
                  supports::kOpQueryOnly);
    }
    {
        // MongerDB 3.0 (mongerd)
        auto mongerd30 = BSON(
            "maxWireVersion" << static_cast<int>(WireVersion::RELEASE_2_7_7) << "minWireVersion"
                             << static_cast<int>(WireVersion::RELEASE_2_4_AND_BEFORE));
        ASSERT_EQ(assertGet(parseProtocolSetFromIsMasterReply(mongerd30)).protocolSet,
                  supports::kOpQueryOnly);
    }
    {
        auto mongerd24 = BSONObj();
        ASSERT_EQ(assertGet(parseProtocolSetFromIsMasterReply(mongerd24)).protocolSet,
                  supports::kOpQueryOnly);
    }
}

#define VALIDATE_WIRE_VERSION(macro, clientMin, clientMax, serverMin, serverMax)            \
    do {                                                                                    \
        auto msg = BSON("minWireVersion" << static_cast<int>(serverMin) << "maxWireVersion" \
                                         << static_cast<int>(serverMax));                   \
        auto swReply = parseProtocolSetFromIsMasterReply(msg);                              \
        ASSERT_OK(swReply.getStatus());                                                     \
        macro(validateWireVersion({clientMin, clientMax}, swReply.getValue().version));     \
    } while (0);

TEST(Protocol, validateWireVersion) {
    /*
     * Test communication with a MongerD latest binary server with downgraded FCV.
     */

    // MongerD 'latest' client -> MongerD 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_OK,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION);

    // MongerD 'latest' client -> MongerD downgraded 'last-stable' server
    VALIDATE_WIRE_VERSION(ASSERT_OK,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION - 2,
                          WireVersion::LATEST_WIRE_VERSION - 1);

    // MongerD 'latest' client -> MongerD upgraded 'last-stable' server
    VALIDATE_WIRE_VERSION(ASSERT_OK,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION - 1);

    // MongerD downgraded 'last-stable' client -> MongerD 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_OK,
                          WireVersion::LATEST_WIRE_VERSION - 2,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION);

    // MongerD upgraded 'last-stable' client -> MongerD 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_OK,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION);

    // MongerS 'latest' client -> MongerD 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_OK,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION);

    // MongerS 'last-stable' client -> MongerD 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_OK,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION);
    /*
     * Test communication with a MongerD latest binary server with upgraded FCV.
     */

    // MongerD 'latest' client -> MongerD 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_OK,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION);

    // MongerD 'latest' client -> MongerD downgraded 'last-stable' server
    VALIDATE_WIRE_VERSION(ASSERT_NOT_OK,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION - 2,
                          WireVersion::LATEST_WIRE_VERSION - 1);

    // MongerD 'latest' client -> MongerD upgraded 'last-stable' server
    VALIDATE_WIRE_VERSION(ASSERT_NOT_OK,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION - 1);

    // MongerD downgraded 'last-stable' client -> MongerD 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_NOT_OK,
                          WireVersion::LATEST_WIRE_VERSION - 2,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION);

    // MongerD upgraded 'last-stable' client -> MongerD 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_NOT_OK,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION);

    // MongerS 'latest' client -> MongerD 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_OK,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION);

    // MongerS 'last-stable' client -> MongerD 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_NOT_OK,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION);

    /*
     * Test communication between MongerD latest binary servers where one has upgraded FCV and the
     * other downgraded FCV.
     */

    // MongerD upgraded 'latest' client -> MongerD downgraded 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_OK,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION);

    // MongerD downgraded 'latest' client -> MongerD upgraded 'latest' server
    VALIDATE_WIRE_VERSION(ASSERT_OK,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION);

    /*
     * Test that it is disallowed for MongerS to communicate with a lower binary server, regardless
     * of FCV.
     */

    // MongerS 'latest' -> MongerD downgraded 'last-stable' server
    VALIDATE_WIRE_VERSION(ASSERT_NOT_OK,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION - 2,
                          WireVersion::LATEST_WIRE_VERSION - 1);

    // MongerS 'latest' -> MongerD upgraded 'last-stable' server
    VALIDATE_WIRE_VERSION(ASSERT_NOT_OK,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION,
                          WireVersion::LATEST_WIRE_VERSION - 1,
                          WireVersion::LATEST_WIRE_VERSION - 1);
}

// A mongers is unable to communicate with a fully upgraded cluster with a higher wire version.
TEST(Protocol, validateWireVersionFailsForUpgradedServerNode) {
    // Server is fully upgraded to the latest wire version.
    auto msg = BSON("minWireVersion" << static_cast<int>(WireVersion::LATEST_WIRE_VERSION)
                                     << "maxWireVersion"
                                     << static_cast<int>(WireVersion::LATEST_WIRE_VERSION));
    auto swReply = parseProtocolSetFromIsMasterReply(msg);
    ASSERT_OK(swReply.getStatus());

    // The client (this mongers server) only has the previous wire version.
    ASSERT_EQUALS(monger::ErrorCodes::IncompatibleWithUpgradedServer,
                  validateWireVersion(
                      {WireVersion::LATEST_WIRE_VERSION - 1, WireVersion::LATEST_WIRE_VERSION - 1},
                      swReply.getValue().version)
                      .code());
}

}  // namespace
