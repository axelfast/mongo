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

#include <cstdint>
#include <functional>

#include "monger/base/string_data.h"
#include "monger/client/connection_string.h"
#include "monger/client/dbclient_base.h"
#include "monger/client/index_spec.h"
#include "monger/client/monger_uri.h"
#include "monger/client/query.h"
#include "monger/client/read_preference.h"
#include "monger/db/dbmessage.h"
#include "monger/db/jsobj.h"
#include "monger/db/write_concern_options.h"
#include "monger/logger/log_severity.h"
#include "monger/platform/atomic_word.h"
#include "monger/rpc/message.h"
#include "monger/rpc/metadata.h"
#include "monger/rpc/op_msg.h"
#include "monger/rpc/protocol.h"
#include "monger/rpc/unique_message.h"
#include "monger/stdx/mutex.h"
#include "monger/transport/message_compressor_manager.h"
#include "monger/transport/session.h"
#include "monger/transport/transport_layer.h"
#include "monger/util/str.h"

namespace monger {

namespace executor {
struct RemoteCommandResponse;
}

class DBClientCursor;
class DBClientCursorBatchIterator;

/**
 *  A basic connection to the database.
 *  This is the main entry point for talking to a simple Monger setup
 *
 *  In general, this type is only allowed to be used from one thread at a time. As a special
 *  exception, it is legal to call shutdownAndDisallowReconnect() from any thread as a way to
 *  interrupt the owning thread.
 */
class DBClientConnection : public DBClientBase {
public:
    using DBClientBase::query;

    /**
     * A hook used to validate the reply of an 'isMaster' command during connection. If the hook
     * returns a non-OK Status, the DBClientConnection object will disconnect from the remote
     * server. This function must not throw - it can only indicate failure by returning a non-OK
     * status.
     */
    using HandshakeValidationHook =
        std::function<Status(const executor::RemoteCommandResponse& isMasterReply)>;

    /**
       @param _autoReconnect if true, automatically reconnect on a connection failure
       @param timeout tcp timeout in seconds - this is for read/write, not connect.
       Connect timeout is fixed, but short, at 5 seconds.
     */
    DBClientConnection(bool _autoReconnect = false,
                       double so_timeout = 0,
                       MongerURI uri = {},
                       const HandshakeValidationHook& hook = HandshakeValidationHook());

    virtual ~DBClientConnection() {
        _numConnections.fetchAndAdd(-1);
    }

    /**
     * Connect to a Monger database server.
     *
     * If autoReconnect is true, you can try to use the DBClientConnection even when
     * false was returned -- it will try to connect again.
     *
     * @param server server to connect to.
     * @param errmsg any relevant error message will appended to the string
     * @return false if fails to connect.
     */
    bool connect(const HostAndPort& server, StringData applicationName, std::string& errmsg);

    /**
     * Semantically equivalent to the previous connect method, but returns a Status
     * instead of taking an errmsg out parameter. Also allows optional validation of the reply to
     * the 'isMaster' command executed during connection.
     *
     * @param server The server to connect to.
     * @param a hook to validate the 'isMaster' reply received during connection. If the hook
     * fails, the connection will be terminated and a non-OK status will be returned.
     */
    virtual Status connect(const HostAndPort& server, StringData applicationName);

    /**
     * This version of connect does not run 'isMaster' after creating a TCP connection to the
     * remote host. This method should be used only when calling 'isMaster' would create a deadlock,
     * such as in 'isSelf'.
     *
     * @param server The server to connect to.
     */
    Status connectSocketOnly(const HostAndPort& server);

    /** Connect to a Monger database server.  Exception throwing version.
        Throws a AssertionException if cannot connect.

       If autoReconnect is true, you can try to use the DBClientConnection even when
       false was returned -- it will try to connect again.

       @param serverHostname host to connect to.  can include port number ( 127.0.0.1 ,
                               127.0.0.1:5555 )
    */

    /**
     * Logs out the connection for the given database.
     *
     * @param dbname the database to logout from.
     * @param info the result object for the logout command (provided for backwards
     *     compatibility with monger shell)
     */
    void logout(const std::string& dbname, BSONObj& info) override;

    std::unique_ptr<DBClientCursor> query(const NamespaceStringOrUUID& nsOrUuid,
                                          Query query = Query(),
                                          int nToReturn = 0,
                                          int nToSkip = 0,
                                          const BSONObj* fieldsToReturn = nullptr,
                                          int queryOptions = 0,
                                          int batchSize = 0) override {
        checkConnection();
        return DBClientBase::query(
            nsOrUuid, query, nToReturn, nToSkip, fieldsToReturn, queryOptions, batchSize);
    }

    unsigned long long query(std::function<void(DBClientCursorBatchIterator&)> f,
                             const NamespaceStringOrUUID& nsOrUuid,
                             Query query,
                             const BSONObj* fieldsToReturn,
                             int queryOptions,
                             int batchSize = 0) override;

    using DBClientBase::runCommandWithTarget;
    std::pair<rpc::UniqueReply, DBClientBase*> runCommandWithTarget(OpMsgRequest request) override;
    std::pair<rpc::UniqueReply, std::shared_ptr<DBClientBase>> runCommandWithTarget(
        OpMsgRequest request, std::shared_ptr<DBClientBase> me) override;

    rpc::UniqueReply parseCommandReplyMessage(const std::string& host,
                                              const Message& replyMsg) override;

    /**
       @return true if this connection is currently in a failed state.  When autoreconnect is on,
               a connection will transition back to an ok state after reconnecting.
     */
    bool isFailed() const override {
        return _failed.load();
    }

    bool isStillConnected() override;

    void setTags(transport::Session::TagMask tag);

    /**
     * Causes an error to be reported the next time the connection is used. Will interrupt
     * operations if they are currently blocked waiting for the network.
     *
     * This is the only method that is allowed to be called from other threads.
     */
    void shutdownAndDisallowReconnect();

    void setWireVersions(int minWireVersion, int maxWireVersion) {
        _minWireVersion = minWireVersion;
        _maxWireVersion = maxWireVersion;
    }

    int getMinWireVersion() final {
        return _minWireVersion;
    }

    int getMaxWireVersion() final {
        return _maxWireVersion;
    }

    std::string toString() const override {
        std::stringstream ss;
        ss << _serverAddress;
        if (!_resolvedAddress.empty())
            ss << " (" << _resolvedAddress << ")";
        if (_failed.load())
            ss << " failed";
        return ss.str();
    }

    std::string getServerAddress() const override {
        return _serverAddress.toString();
    }
    const HostAndPort& getServerHostAndPort() const {
        return _serverAddress;
    }

    void say(Message& toSend, bool isRetry = false, std::string* actualServer = nullptr) override;
    bool recv(Message& m, int lastRequestId) override;
    void checkResponse(const std::vector<BSONObj>& batch,
                       bool networkError,
                       bool* retry = nullptr,
                       std::string* host = nullptr) override;
    bool call(Message& toSend,
              Message& response,
              bool assertOk,
              std::string* actualServer) override;
    ConnectionString::ConnectionType type() const override {
        return ConnectionString::MASTER;
    }
    void setSoTimeout(double timeout);
    double getSoTimeout() const override {
        return _socketTimeout.value_or(Milliseconds{0}).count() / 1000.0;
    }

    bool lazySupported() const override {
        return true;
    }

    static int getNumConnections() {
        return _numConnections.load();
    }

    /**
     * Set the name of the replica set that this connection is associated to.
     * Note: There is no validation on replSetName.
     */
    void setParentReplSetName(const std::string& replSetName);

    uint64_t getSockCreationMicroSec() const override;

    MessageCompressorManager& getCompressorManager() {
        return _compressorManager;
    }

    // throws a NetworkException if in failed state and not reconnecting or if waiting to reconnect
    void checkConnection() override {
        if (_failed.load())
            _checkConnection();
    }

    bool isReplicaSetMember() const override {
        return _isReplicaSetMember;
    }

    bool isMongers() const override {
        return _isMongers;
    }

    Status authenticateInternalUser() override;

protected:
    int _minWireVersion{0};
    int _maxWireVersion{0};
    bool _isReplicaSetMember = false;
    bool _isMongers = false;

    void _auth(const BSONObj& params) override;

    // The session mutex must be held to shutdown the _session from a non-owning thread, or to
    // rebind the handle from the owning thread. The thread that owns this DBClientConnection is
    // allowed to use the _session without locking the mutex. This mutex also guards writes to
    // _stayFailed, although reads are allowed outside the mutex.
    stdx::mutex _sessionMutex;
    transport::SessionHandle _session;
    boost::optional<Milliseconds> _socketTimeout;
    transport::Session::TagMask _tagMask = transport::Session::kEmptyTagMask;
    uint64_t _sessionCreationMicros = INVALID_SOCK_CREATION_TIME;
    Date_t _lastConnectivityCheck;

    AtomicWord<bool> _stayFailed{false};
    AtomicWord<bool> _failed{false};
    const bool autoReconnect;
    Backoff _autoReconnectBackoff;

    HostAndPort _serverAddress;
    std::string _resolvedAddress;
    std::string _applicationName;

    void _checkConnection();

    bool _internalAuthOnReconnect = false;
    std::map<std::string, BSONObj> authCache;

    static AtomicWord<int> _numConnections;

private:
    /**
     * Inspects the contents of 'replyBody' and informs the replica set monitor that the host 'this'
     * is connected with is no longer the primary if a "not master" error message or error code was
     * returned.
     */
    void handleNotMasterResponse(const BSONObj& replyBody, StringData errorMsgFieldName);
    enum FailAction { kSetFlag, kEndSession, kReleaseSession };
    void _markFailed(FailAction action);

    // Contains the string for the replica set name of the host this is connected to.
    // Should be empty if this connection is not pointing to a replica set member.
    std::string _parentReplSetName;

    // Hook ran on every call to connect()
    HandshakeValidationHook _hook;

    MessageCompressorManager _compressorManager;

    MongerURI _uri;
};

BSONElement getErrField(const BSONObj& result);
bool hasErrField(const BSONObj& result);

}  // namespace monger
