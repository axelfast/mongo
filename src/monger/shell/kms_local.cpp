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

#include <kms_message/kms_message.h>

#include <stdlib.h>

#include "monger/base/init.h"
#include "monger/base/secure_allocator.h"
#include "monger/base/status_with.h"
#include "monger/bson/json.h"
#include "monger/crypto/aead_encryption.h"
#include "monger/crypto/symmetric_crypto.h"
#include "monger/crypto/symmetric_key.h"
#include "monger/shell/kms.h"
#include "monger/shell/kms_gen.h"
#include "monger/util/base64.h"

namespace monger {
namespace {

/**
 * Manages Local KMS Information
 */
class LocalKMSService : public KMSService {
public:
    LocalKMSService(SymmetricKey key) : _key(std::move(key)) {}
    ~LocalKMSService() final = default;

    static std::unique_ptr<KMSService> create(const LocalKMS& config);

    std::vector<uint8_t> encrypt(ConstDataRange cdr, StringData kmsKeyId) final;

    SecureVector<uint8_t> decrypt(ConstDataRange cdr, BSONObj masterKey) final;

    BSONObj encryptDataKey(ConstDataRange cdr, StringData keyId) final;

private:
    // Key that wraps all KMS encrypted data
    SymmetricKey _key;
};

std::vector<uint8_t> LocalKMSService::encrypt(ConstDataRange cdr, StringData kmsKeyId) {
    std::vector<std::uint8_t> ciphertext(crypto::aeadCipherOutputLength(cdr.length()));

    uassertStatusOK(crypto::aeadEncrypt(_key,
                                        reinterpret_cast<const uint8_t*>(cdr.data()),
                                        cdr.length(),
                                        nullptr,
                                        0,
                                        ciphertext.data(),
                                        ciphertext.size()));

    return ciphertext;
}

BSONObj LocalKMSService::encryptDataKey(ConstDataRange cdr, StringData keyId) {
    auto dataKey = encrypt(cdr, keyId);

    LocalMasterKey masterKey;

    LocalMasterKeyAndMaterial keyAndMaterial;
    keyAndMaterial.setKeyMaterial(dataKey);
    keyAndMaterial.setMasterKey(masterKey);

    return keyAndMaterial.toBSON();
}

SecureVector<uint8_t> LocalKMSService::decrypt(ConstDataRange cdr, BSONObj masterKey) {
    SecureVector<uint8_t> plaintext(cdr.length());

    size_t outLen = plaintext->size();
    uassertStatusOK(crypto::aeadDecrypt(_key,
                                        reinterpret_cast<const uint8_t*>(cdr.data()),
                                        cdr.length(),
                                        nullptr,
                                        0,
                                        plaintext->data(),
                                        &outLen));
    plaintext->resize(outLen);

    return plaintext;
}

std::unique_ptr<KMSService> LocalKMSService::create(const LocalKMS& config) {
    uassert(51237,
            str::stream() << "Local KMS key must be 64 bytes, found " << config.getKey().length()
                          << " bytes instead",
            config.getKey().length() == crypto::kAeadAesHmacKeySize);

    SecureVector<uint8_t> aesVector = SecureVector<uint8_t>(
        config.getKey().data(), config.getKey().data() + config.getKey().length());
    SymmetricKey key = SymmetricKey(aesVector, crypto::aesAlgorithm, "local");

    auto localKMS = std::make_unique<LocalKMSService>(std::move(key));

    return localKMS;
}

/**
 * Factory for LocalKMSService if user specifies local config to monger() JS constructor.
 */
class LocalKMSServiceFactory final : public KMSServiceFactory {
public:
    LocalKMSServiceFactory() = default;
    ~LocalKMSServiceFactory() = default;

    std::unique_ptr<KMSService> create(const BSONObj& config) final {
        auto field = config[KmsProviders::kLocalFieldName];
        if (field.eoo()) {
            return nullptr;
        }

        auto obj = field.Obj();
        return LocalKMSService::create(LocalKMS::parse(IDLParserErrorContext("root"), obj));
    }
};

}  // namspace

MONGO_INITIALIZER(LocalKMSRegister)(::monger::InitializerContext* context) {
    KMSServiceController::registerFactory(KMSProviderEnum::local,
                                          std::make_unique<LocalKMSServiceFactory>());
    return Status::OK();
}

}  // namespace monger
