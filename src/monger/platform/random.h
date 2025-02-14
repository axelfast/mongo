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
#include <limits>
#include <memory>

namespace monger {

/**
 * Uses http://en.wikipedia.org/wiki/Xorshift
 */
class PseudoRandom {
public:
    PseudoRandom(int32_t seed);

    PseudoRandom(uint32_t seed);

    PseudoRandom(int64_t seed);

    int32_t nextInt32();

    int64_t nextInt64();

    /**
     * Returns a random number in the range [0, 1).
     */
    double nextCanonicalDouble();

    /**
     * @return a number between 0 and max
     */
    int32_t nextInt32(int32_t max) {
        return static_cast<uint32_t>(nextInt32()) % static_cast<uint32_t>(max);
    }

    /**
     * @return a number between 0 and max
     */
    int64_t nextInt64(int64_t max) {
        return static_cast<uint64_t>(nextInt64()) % static_cast<uint64_t>(max);
    }

    /**
     * This returns an object that adapts PseudoRandom such that it
     * can be used as the third argument to std::shuffle. Note that
     * the lifetime of the returned object must be a subset of the
     * lifetime of the PseudoRandom object.
     */
    auto urbg() {

        class URBG {
        public:
            explicit URBG(PseudoRandom* impl) : _impl(impl) {}

            using result_type = uint64_t;

            static constexpr result_type min() {
                return std::numeric_limits<result_type>::min();
            }

            static constexpr result_type max() {
                return std::numeric_limits<result_type>::max();
            }

            result_type operator()() {
                return _impl->nextInt64();
            }

        private:
            PseudoRandom* _impl;
        };

        return URBG(this);
    }

private:
    uint32_t nextUInt32();

    uint32_t _x;
    uint32_t _y;
    uint32_t _z;
    uint32_t _w;
};

/**
 * More secure random numbers
 * Suitable for nonce/crypto
 * Slower than PseudoRandom, so only use when really need
 */
class SecureRandom {
public:
    virtual ~SecureRandom();

    virtual int64_t nextInt64() = 0;

    static std::unique_ptr<SecureRandom> create();
};
}  // namespace monger
