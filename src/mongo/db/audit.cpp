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

#include "monger/db/audit.h"

#if !MONGO_ENTERPRISE_AUDIT

void monger::audit::logAuthentication(Client* client,
                                     StringData mechanism,
                                     const UserName& user,
                                     ErrorCodes::Error result) {}

void monger::audit::logCommandAuthzCheck(Client* client,
                                        const OpMsgRequest& cmdObj,
                                        const CommandInterface& command,
                                        ErrorCodes::Error result) {}

void monger::audit::logDeleteAuthzCheck(Client* client,
                                       const NamespaceString& ns,
                                       const BSONObj& pattern,
                                       ErrorCodes::Error result) {}

void monger::audit::logGetMoreAuthzCheck(Client* client,
                                        const NamespaceString& ns,
                                        long long cursorId,
                                        ErrorCodes::Error result) {}

void monger::audit::logInsertAuthzCheck(Client* client,
                                       const NamespaceString& ns,
                                       const BSONObj& insertedObj,
                                       ErrorCodes::Error result) {}

void monger::audit::logKillCursorsAuthzCheck(Client* client,
                                            const NamespaceString& ns,
                                            long long cursorId,
                                            ErrorCodes::Error result) {}

void monger::audit::logQueryAuthzCheck(Client* client,
                                      const NamespaceString& ns,
                                      const BSONObj& query,
                                      ErrorCodes::Error result) {}

void monger::audit::logUpdateAuthzCheck(Client* client,
                                       const NamespaceString& ns,
                                       const BSONObj& query,
                                       const write_ops::UpdateModification& update,
                                       bool isUpsert,
                                       bool isMulti,
                                       ErrorCodes::Error result) {}

void monger::audit::logCreateUser(Client* client,
                                 const UserName& username,
                                 bool password,
                                 const BSONObj* customData,
                                 const std::vector<RoleName>& roles,
                                 const boost::optional<BSONArray>& restrictions) {}

void monger::audit::logDropUser(Client* client, const UserName& username) {}

void monger::audit::logDropAllUsersFromDatabase(Client* client, StringData dbname) {}

void monger::audit::logUpdateUser(Client* client,
                                 const UserName& username,
                                 bool password,
                                 const BSONObj* customData,
                                 const std::vector<RoleName>* roles,
                                 const boost::optional<BSONArray>& restrictions) {}

void monger::audit::logGrantRolesToUser(Client* client,
                                       const UserName& username,
                                       const std::vector<RoleName>& roles) {}

void monger::audit::logRevokeRolesFromUser(Client* client,
                                          const UserName& username,
                                          const std::vector<RoleName>& roles) {}

void monger::audit::logCreateRole(Client* client,
                                 const RoleName& role,
                                 const std::vector<RoleName>& roles,
                                 const PrivilegeVector& privileges,
                                 const boost::optional<BSONArray>& restrictions) {}

void monger::audit::logUpdateRole(Client* client,
                                 const RoleName& role,
                                 const std::vector<RoleName>* roles,
                                 const PrivilegeVector* privileges,
                                 const boost::optional<BSONArray>& restrictions) {}

void monger::audit::logDropRole(Client* client, const RoleName& role) {}

void monger::audit::logDropAllRolesFromDatabase(Client* client, StringData dbname) {}

void monger::audit::logGrantRolesToRole(Client* client,
                                       const RoleName& role,
                                       const std::vector<RoleName>& roles) {}

void monger::audit::logRevokeRolesFromRole(Client* client,
                                          const RoleName& role,
                                          const std::vector<RoleName>& roles) {}

void monger::audit::logGrantPrivilegesToRole(Client* client,
                                            const RoleName& role,
                                            const PrivilegeVector& privileges) {}

void monger::audit::logRevokePrivilegesFromRole(Client* client,
                                               const RoleName& role,
                                               const PrivilegeVector& privileges) {}

void monger::audit::logReplSetReconfig(Client* client,
                                      const BSONObj* oldConfig,
                                      const BSONObj* newConfig) {}

void monger::audit::logApplicationMessage(Client* client, StringData msg) {}

void monger::audit::logShutdown(Client* client) {}

void monger::audit::logCreateIndex(Client* client,
                                  const BSONObj* indexSpec,
                                  StringData indexname,
                                  StringData nsname) {}

void monger::audit::logCreateCollection(Client* client, StringData nsname) {}

void monger::audit::logCreateDatabase(Client* client, StringData dbname) {}


void monger::audit::logDropIndex(Client* client, StringData indexname, StringData nsname) {}

void monger::audit::logDropCollection(Client* client, StringData nsname) {}

void monger::audit::logDropDatabase(Client* client, StringData dbname) {}

void monger::audit::logRenameCollection(Client* client, StringData source, StringData target) {}

void monger::audit::logEnableSharding(Client* client, StringData dbname) {}

void monger::audit::logAddShard(Client* client,
                               StringData name,
                               const std::string& servers,
                               long long maxSize) {}

void monger::audit::logRemoveShard(Client* client, StringData shardname) {}

void monger::audit::logShardCollection(Client* client,
                                      StringData ns,
                                      const BSONObj& keyPattern,
                                      bool unique) {}

#endif
