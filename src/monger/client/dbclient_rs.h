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

/**
 * Connect to a Replica Set, from C++.
 */

#include <utility>

#include "monger/client/dbclient_connection.h"
#include "monger/client/monger_uri.h"
#include "monger/util/net/hostandport.h"

namespace monger {

class ReplicaSetMonitor;
class TagSet;
struct ReadPreferenceSetting;
typedef std::shared_ptr<ReplicaSetMonitor> ReplicaSetMonitorPtr;

/** Use this class to connect to a replica set of servers.  The class will manage
   checking for which server in a replica set is master, and do failover automatically.

   This can also be used to connect to replica pairs since pairs are a subset of sets

   On a failover situation, expect at least one operation to return an error (throw
   an exception) before the failover is complete.  Operations are not retried.
*/
class DBClientReplicaSet : public DBClientBase {
public:
    using DBClientBase::query;
    using DBClientBase::update;
    using DBClientBase::remove;

    /** Call connect() after constructing. autoReconnect is always on for DBClientReplicaSet
     * connections. */
    DBClientReplicaSet(const std::string& name,
                       const std::vector<HostAndPort>& servers,
                       StringData applicationName,
                       double so_timeout = 0,
                       MongerURI uri = {});

    /**
     * Returns false if no member of the set were reachable. This object
     * can still be used even when false was returned as it will try to
     * reconnect when you use it later.
     */
    bool connect();

    Status authenticateInternalUser() override;

    /**
     * Logs out the connection for the given database.
     *
     * @param dbname the database to logout from.
     * @param info the result object for the logout command (provided for backwards
     *     compatibility with monger shell)
     */
    void logout(const std::string& dbname, BSONObj& info) override;

    // ----------- simple functions --------------

    /** throws userassertion "no master found" */
    std::unique_ptr<DBClientCursor> query(const NamespaceStringOrUUID& nsOrUuid,
                                          Query query,
                                          int nToReturn = 0,
                                          int nToSkip = 0,
                                          const BSONObj* fieldsToReturn = nullptr,
                                          int queryOptions = 0,
                                          int batchSize = 0) override;

    /** throws userassertion "no master found" */
    BSONObj findOne(const std::string& ns,
                    const Query& query,
                    const BSONObj* fieldsToReturn = nullptr,
                    int queryOptions = 0) override;

    void insert(const std::string& ns, BSONObj obj, int flags = 0) override;

    /** insert multiple objects.  Note that single object insert is asynchronous, so this version
        is only nominally faster and not worth a special effort to try to use.  */
    void insert(const std::string& ns, const std::vector<BSONObj>& v, int flags = 0) override;

    void remove(const std::string& ns, Query obj, int flags) override;

    void update(const std::string& ns, Query query, BSONObj obj, int flags) override;

    void killCursor(const NamespaceString& ns, long long cursorID) override;

    // ---- access raw connections ----

    /**
     * WARNING: this method is very dangerous - this object can decide to free the
     *     returned master connection any time.
     *
     * @return the reference to the address that points to the master connection.
     */
    DBClientConnection& masterConn();

    /**
     * WARNING: this method is very dangerous - this object can decide to free the
     *     returned master connection any time. This can also unpin the cached
     *     slaveOk/read preference connection.
     *
     * @return the reference to the address that points to a secondary connection.
     */
    DBClientConnection& slaveConn();

    // ---- callback pieces -------

    void say(Message& toSend, bool isRetry = false, std::string* actualServer = nullptr) override;
    bool recv(Message& toRecv, int lastRequestId) override;
    void checkResponse(const std::vector<BSONObj>& batch,
                       bool networkError,
                       bool* retry = nullptr,
                       std::string* targetHost = nullptr) override;

    /* this is the callback from our underlying connections to notify us that we got a "not master"
     * error.
     */
    void isntMaster();

    /* this is used to indicate we got a "not master or secondary" error from a secondary.
     */
    void isntSecondary();

    // ----- status ------

    bool isFailed() const override {
        return !_master || _master->isFailed();
    }
    bool isStillConnected() override;

    // ----- informational ----

    /**
     * Gets the replica set name of the set we are connected to.
     */
    const std::string& getSetName() const {
        return _setName;
    }

    /**
     * Returns the HostAndPort of the server this connection believes belongs to the primary,
     * or returns an empty HostAndPort if it doesn't know about a current primary.
     */
    HostAndPort getSuspectedPrimaryHostAndPort() const;

    double getSoTimeout() const override {
        return _so_timeout;
    }

    std::string toString() const override {
        return getServerAddress();
    }

    std::string getServerAddress() const override;

    ConnectionString::ConnectionType type() const override {
        return ConnectionString::SET;
    }
    bool lazySupported() const override {
        return true;
    }

    using DBClientBase::runCommandWithTarget;
    std::pair<rpc::UniqueReply, DBClientBase*> runCommandWithTarget(OpMsgRequest request) final;
    std::pair<rpc::UniqueReply, std::shared_ptr<DBClientBase>> runCommandWithTarget(
        OpMsgRequest request, std::shared_ptr<DBClientBase> me) final;
    DBClientBase* runFireAndForgetCommand(OpMsgRequest request) final;

    void setRequestMetadataWriter(rpc::RequestMetadataWriter writer) final;

    void setReplyMetadataReader(rpc::ReplyMetadataReader reader) final;

    int getMinWireVersion() final;
    int getMaxWireVersion() final;
    // ---- low level ------

    bool call(Message& toSend,
              Message& response,
              bool assertOk,
              std::string* actualServer) override;

    /**
     * Returns whether a query or command can be sent to secondaries based on the query object
     * and options.
     *
     * @param ns the namespace of the query.
     * @param queryObj the query object to check.
     * @param queryOptions the query options
     *
     * @return true if the query/cmd could potentially be sent to a secondary, false otherwise
     */
    static bool isSecondaryQuery(const std::string& ns, const BSONObj& queryObj, int queryOptions);

    /**
     * Performs a "soft reset" by clearing all states relating to secondary nodes and
     * returning secondary connections to the pool.
     */
    void reset() override;

    bool isReplicaSetMember() const override {
        return true;
    }

    bool isMongers() const override {
        return false;
    }

    /**
     * @bool setting if true, DBClientReplicaSet connections will make sure that secondary
     *    connections are authenticated and log them before returning them to the pool.
     */
    static void setAuthPooledSecondaryConn(bool setting);

protected:
    /** Authorize.  Authorizes all nodes as needed
    */
    void _auth(const BSONObj& params) override;

private:
    /**
     * Used to simplify slave-handling logic on errors
     *
     * @return back the passed cursor
     * @throws DBException if the directed node cannot accept the query because it
     *     is not a master
     */
    std::unique_ptr<DBClientCursor> checkSlaveQueryResult(std::unique_ptr<DBClientCursor> result);

    DBClientConnection* checkMaster();

    template <typename Authenticate>
    Status _runAuthLoop(Authenticate authCb);

    /**
     * Helper method for selecting a node based on the read preference. Will advance
     * the tag tags object if it cannot find a node that matches the current tag.
     *
     * @param readPref the preference to use for selecting a node.
     *
     * @return a pointer to the new connection object if it can find a good connection.
     *     Otherwise it returns NULL.
     *
     * @throws DBException when an error occurred either when trying to connect to
     *     a node that was thought to be ok or when an assertion happened.
     */
    DBClientConnection* selectNodeUsingTags(std::shared_ptr<ReadPreferenceSetting> readPref);

    /**
     * @return true if the last host used in the last slaveOk query is still in the
     * set and can be used for the given read preference.
     */
    bool checkLastHost(const ReadPreferenceSetting* readPref);

    /**
     * Destroys all cached information about the last slaveOk operation and reports the host as
     * failed in the replica set monitor with the specified 'status'.
     */
    void _invalidateLastSlaveOkCache(const Status& status);

    void _authConnection(DBClientConnection* conn);

    /**
     * Calls logout on the connection for all known database this DBClientRS instance has
     * logged in.
     */
    void logoutAll(DBClientConnection* conn);

    /**
     * Clears the master connection.
     */
    void resetMaster();

    /**
     * Clears the slaveOk connection and returns it to the pool if not the same as _master.
     */
    void resetSlaveOkConn();

    // TODO: remove this when processes other than mongers uses the driver version.
    static bool _authPooledSecondaryConn;

    // Throws a DBException if the monitor doesn't exist and there isn't a cached seed to use.
    ReplicaSetMonitorPtr _getMonitor();

    std::string _setName;
    std::string _applicationName;
    std::shared_ptr<ReplicaSetMonitor> _rsm;

    HostAndPort _masterHost;
    std::shared_ptr<DBClientConnection> _master;

    // Last used host in a slaveOk query (can be a primary).
    HostAndPort _lastSlaveOkHost;
    // Last used connection in a slaveOk query (can be a primary).
    // Connection can either be owned here or returned to the connection pool. Note that
    // if connection is primary, it is owned by _master so it is incorrect to return
    // it to the pool.
    std::shared_ptr<DBClientConnection> _lastSlaveOkConn;
    std::shared_ptr<ReadPreferenceSetting> _lastReadPref;

    double _so_timeout;

    // we need to store so that when we connect to a new node on failure
    // we can re-auth
    // this could be a security issue, as the password is stored in memory
    // not sure if/how we should handle
    bool _internalAuthRequested = false;
    std::map<std::string, BSONObj> _auths;  // dbName -> auth parameters

    MongerURI _uri;

protected:
    /**
     * for storing (non-threadsafe) information between lazy calls
     */
    class LazyState {
    public:
        LazyState() : _lastClient(nullptr), _lastOp(-1), _secondaryQueryOk(false), _retries(0) {}
        DBClientConnection* _lastClient;
        int _lastOp;
        bool _secondaryQueryOk;
        int _retries;

    } _lazyState;
};
}
