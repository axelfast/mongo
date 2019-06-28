/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include "monger/base/data_cursor.h"
#include "monger/base/data_type_validated.h"
#include "monger/bson/bson_depth.h"
#include "monger/client/dbclient_base.h"
#include "monger/crypto/aead_encryption.h"
#include "monger/crypto/symmetric_crypto.h"
#include "monger/db/client.h"
#include "monger/db/commands.h"
#include "monger/db/matcher/schema/encrypt_schema_gen.h"
#include "monger/db/namespace_string.h"
#include "monger/rpc/object_check.h"
#include "monger/rpc/op_msg_rpc_impls.h"
#include "monger/scripting/mozjs/bindata.h"
#include "monger/scripting/mozjs/implscope.h"
#include "monger/scripting/mozjs/maxkey.h"
#include "monger/scripting/mozjs/minkey.h"
#include "monger/scripting/mozjs/monger.h"
#include "monger/scripting/mozjs/objectwrapper.h"
#include "monger/scripting/mozjs/valuereader.h"
#include "monger/scripting/mozjs/valuewriter.h"
#include "monger/shell/encrypted_shell_options.h"
#include "monger/shell/kms.h"
#include "monger/shell/kms_gen.h"
#include "monger/shell/shell_options.h"
#include "monger/util/lru_cache.h"

namespace monger {

constexpr std::size_t kEncryptedDBCacheSize = 50;

constexpr int kAssociatedDataLength = 18;
constexpr uint8_t kIntentToEncryptBit = 0x00;
constexpr uint8_t kDeterministicEncryptionBit = 0x01;
constexpr uint8_t kRandomEncryptionBit = 0x02;

class EncryptedDBClientBase : public DBClientBase, public mozjs::EncryptionCallbacks {
public:
    EncryptedDBClientBase(std::unique_ptr<DBClientBase> conn,
                          ClientSideFLEOptions encryptionOptions,
                          JS::HandleValue collection,
                          JSContext* cx);


    std::string getServerAddress() const final;

    bool call(Message& toSend, Message& response, bool assertOk, std::string* actualServer) final;

    void say(Message& toSend, bool isRetry, std::string* actualServer) final;

    bool lazySupported() const final;

    using DBClientBase::runCommandWithTarget;
    virtual std::pair<rpc::UniqueReply, DBClientBase*> runCommandWithTarget(
        OpMsgRequest request) override;
    std::string toString() const final;

    int getMinWireVersion() final;

    int getMaxWireVersion() final;

    using EncryptionCallbacks::generateDataKey;
    void generateDataKey(JSContext* cx, JS::CallArgs args) final;

    using EncryptionCallbacks::getDataKeyCollection;
    void getDataKeyCollection(JSContext* cx, JS::CallArgs args) final;

    using EncryptionCallbacks::encrypt;
    void encrypt(mozjs::MozJSImplScope* scope, JSContext* cx, JS::CallArgs args) final;

    using EncryptionCallbacks::decrypt;
    void decrypt(mozjs::MozJSImplScope* scope, JSContext* cx, JS::CallArgs args) final;

    using EncryptionCallbacks::trace;
    void trace(JSTracer* trc) final;

    using DBClientBase::query;
    std::unique_ptr<DBClientCursor> query(const NamespaceStringOrUUID& nsOrUuid,
                                          Query query,
                                          int nToReturn,
                                          int nToSkip,
                                          const BSONObj* fieldsToReturn,
                                          int queryOptions,
                                          int batchSize) final;

    bool isFailed() const final;

    bool isStillConnected() final;

    ConnectionString::ConnectionType type() const final;

    double getSoTimeout() const final;

    bool isReplicaSetMember() const final;

    bool isMongers() const final;

protected:
    JS::Value getCollection() const;

    BSONObj validateBSONElement(ConstDataRange out, uint8_t bsonType);

    NamespaceString getCollectionNS();

    std::shared_ptr<SymmetricKey> getDataKey(const UUID& uuid);

    std::vector<uint8_t> encryptWithKey(UUID uuid,
                                        const std::shared_ptr<SymmetricKey>& key,
                                        ConstDataRange plaintext,
                                        BSONType bsonType,
                                        int32_t algorithm);

private:
    std::vector<uint8_t> getBinDataArg(mozjs::MozJSImplScope* scope,
                                       JSContext* cx,
                                       JS::CallArgs args,
                                       int index,
                                       BinDataType type);

    std::shared_ptr<SymmetricKey> getDataKeyFromDisk(const UUID& uuid);

protected:
    std::unique_ptr<DBClientBase> _conn;
    ClientSideFLEOptions _encryptionOptions;

private:
    LRUCache<UUID, std::pair<std::shared_ptr<SymmetricKey>, Date_t>, UUID::Hash> _datakeyCache{
        kEncryptedDBCacheSize};
    JS::Heap<JS::Value> _collection;
    JSContext* _cx;
};

using ImplicitEncryptedDBClientCallback =
    std::unique_ptr<DBClientBase>(std::unique_ptr<DBClientBase> conn,
                                  ClientSideFLEOptions encryptionOptions,
                                  JS::HandleValue collection,
                                  JSContext* cx);
void setImplicitEncryptedDBClientCallback(ImplicitEncryptedDBClientCallback* callback);


}  // namespace monger
