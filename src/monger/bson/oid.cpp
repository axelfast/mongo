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

#include "monger/bson/oid.h"

#include <boost/functional/hash.hpp>
#include <limits>
#include <memory>

#include "monger/base/init.h"
#include "monger/bson/bsonobjbuilder.h"
#include "monger/platform/atomic_word.h"
#include "monger/platform/random.h"
#include "monger/util/hex.h"

namespace monger {

namespace {
std::unique_ptr<AtomicWord<int64_t>> counter;

const std::size_t kTimestampOffset = 0;
const std::size_t kInstanceUniqueOffset = kTimestampOffset + OID::kTimestampSize;
const std::size_t kIncrementOffset = kInstanceUniqueOffset + OID::kInstanceUniqueSize;
OID::InstanceUnique _instanceUnique;
}  // namespace

MONGO_INITIALIZER_GENERAL(OIDGeneration, MONGO_NO_PREREQUISITES, ("default"))
(InitializerContext* context) {
    std::unique_ptr<SecureRandom> entropy(SecureRandom::create());
    counter = std::make_unique<AtomicWord<int64_t>>(entropy->nextInt64());
    _instanceUnique = OID::InstanceUnique::generate(*entropy);
    return Status::OK();
}

OID::Increment OID::Increment::next() {
    uint64_t nextCtr = counter->fetchAndAdd(1);
    OID::Increment incr;

    incr.bytes[0] = uint8_t(nextCtr >> 16);
    incr.bytes[1] = uint8_t(nextCtr >> 8);
    incr.bytes[2] = uint8_t(nextCtr);

    return incr;
}

OID::InstanceUnique OID::InstanceUnique::generate(SecureRandom& entropy) {
    int64_t rand = entropy.nextInt64();
    OID::InstanceUnique u;
    std::memcpy(u.bytes, &rand, kInstanceUniqueSize);
    return u;
}

void OID::setTimestamp(const OID::Timestamp timestamp) {
    _view().write<BigEndian<Timestamp>>(timestamp, kTimestampOffset);
}

void OID::setInstanceUnique(const OID::InstanceUnique unique) {
    // Byte order doesn't matter here
    _view().write<InstanceUnique>(unique, kInstanceUniqueOffset);
}

void OID::setIncrement(const OID::Increment inc) {
    _view().write<Increment>(inc, kIncrementOffset);
}

OID::Timestamp OID::getTimestamp() const {
    return view().read<BigEndian<Timestamp>>(kTimestampOffset);
}

OID::InstanceUnique OID::getInstanceUnique() const {
    // Byte order doesn't matter here
    return view().read<InstanceUnique>(kInstanceUniqueOffset);
}

OID::Increment OID::getIncrement() const {
    return view().read<Increment>(kIncrementOffset);
}

void OID::hash_combine(size_t& seed) const {
    uint32_t v;
    for (int i = 0; i != kOIDSize; i += sizeof(uint32_t)) {
        memcpy(&v, _data + i, sizeof(uint32_t));
        boost::hash_combine(seed, v);
    }
}

size_t OID::Hasher::operator()(const OID& oid) const {
    size_t seed = 0;
    oid.hash_combine(seed);
    return seed;
}

void OID::regenMachineId() {
    std::unique_ptr<SecureRandom> entropy(SecureRandom::create());
    _instanceUnique = InstanceUnique::generate(*entropy);
}

unsigned OID::getMachineId() {
    uint32_t ret = 0;
    std::memcpy(&ret, _instanceUnique.bytes, sizeof(uint32_t));
    return ret;
}

void OID::justForked() {
    regenMachineId();
}

void OID::init() {
    // each set* method handles endianness
    setTimestamp(time(nullptr));
    setInstanceUnique(_instanceUnique);
    setIncrement(Increment::next());
}

void OID::initFromTermNumber(int64_t term) {
    // Each set* method handles endianness.
    // Set max timestamp because the drivers compare ElectionId's to determine valid new primaries,
    // and we want ElectionId's with terms to supercede ones without terms.
    setTimestamp(std::numeric_limits<Timestamp>::max());
    _view().write<BigEndian<int64_t>>(term, kInstanceUniqueOffset);
}

void OID::init(const std::string& s) {
    verify(s.size() == 24);
    const char* p = s.c_str();
    for (std::size_t i = 0; i < kOIDSize; i++) {
        _data[i] = uassertStatusOK(fromHex(p));
        p += 2;
    }
}

void OID::init(Date_t date, bool max) {
    setTimestamp(uint32_t(date.toMillisSinceEpoch() / 1000));
    uint64_t rest = max ? std::numeric_limits<uint64_t>::max() : 0u;
    std::memcpy(_view().view(kInstanceUniqueOffset), &rest, kInstanceUniqueSize + kIncrementSize);
}

time_t OID::asTimeT() const {
    return getTimestamp();
}

std::string OID::toString() const {
    return toHexLower(_data, kOIDSize);
}

std::string OID::toIncString() const {
    return toHexLower(getIncrement().bytes, kIncrementSize);
}

}  // namespace monger
