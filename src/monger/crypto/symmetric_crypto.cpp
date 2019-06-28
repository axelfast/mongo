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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kDefault

#include "monger/platform/basic.h"

#include "monger/crypto/symmetric_crypto.h"

#include <memory>

#include "monger/base/data_cursor.h"
#include "monger/base/init.h"
#include "monger/base/status.h"
#include "monger/crypto/symmetric_key.h"
#include "monger/platform/random.h"
#include "monger/util/assert_util.h"
#include "monger/util/log.h"
#include "monger/util/net/ssl_manager.h"
#include "monger/util/str.h"

namespace monger {
namespace crypto {

namespace {
std::unique_ptr<SecureRandom> random;
}  // namespace

MONGO_INITIALIZER(CreateKeyEntropySource)(InitializerContext* context) {
    random = std::unique_ptr<SecureRandom>(SecureRandom::create());
    return Status::OK();
}

size_t aesGetIVSize(crypto::aesMode mode) {
    switch (mode) {
        case crypto::aesMode::cbc:
            return crypto::aesCBCIVSize;
        case crypto::aesMode::gcm:
            return crypto::aesGCMIVSize;
        default:
            fassertFailed(4053);
    }
}

aesMode getCipherModeFromString(const std::string& mode) {
    if (mode == aes256CBCName) {
        return aesMode::cbc;
    } else if (mode == aes256GCMName) {
        return aesMode::gcm;
    } else {
        MONGO_UNREACHABLE;
    }
}

std::string getStringFromCipherMode(aesMode mode) {
    if (mode == aesMode::cbc) {
        return aes256CBCName;
    } else if (mode == aesMode::gcm) {
        return aes256GCMName;
    } else {
        MONGO_UNREACHABLE;
    }
}

SymmetricKey aesGenerate(size_t keySize, SymmetricKeyId keyId) {
    invariant(keySize == sym256KeySize);

    SecureVector<uint8_t> key(keySize);

    size_t offset = 0;
    while (offset < keySize) {
        std::uint64_t randomValue = random->nextInt64();
        memcpy(key->data() + offset, &randomValue, sizeof(randomValue));
        offset += sizeof(randomValue);
    }

    return SymmetricKey(std::move(key), aesAlgorithm, std::move(keyId));
}

}  // namespace crypto
}  // namespace monger
