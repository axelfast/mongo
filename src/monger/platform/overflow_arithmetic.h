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

#ifdef _MSC_VER
#include <SafeInt.hpp>
#endif

namespace monger {

/**
 * Returns true if multiplying lhs by rhs would overflow. Otherwise, multiplies 64-bit signed
 * or unsigned integers lhs by rhs and stores the result in *product.
 */
constexpr bool mongerSignedMultiplyOverflow64(int64_t lhs, int64_t rhs, int64_t* product);
constexpr bool mongerUnsignedMultiplyOverflow64(uint64_t lhs, uint64_t rhs, uint64_t* product);

/**
 * Returns true if adding lhs and rhs would overflow. Otherwise, adds 64-bit signed or unsigned
 * integers lhs and rhs and stores the result in *sum.
 */
constexpr bool mongerSignedAddOverflow64(int64_t lhs, int64_t rhs, int64_t* sum);
constexpr bool mongerUnsignedAddOverflow64(uint64_t lhs, uint64_t rhs, uint64_t* sum);

/**
 * Returns true if subtracting rhs from lhs would overflow. Otherwise, subtracts 64-bit signed or
 * unsigned integers rhs from lhs and stores the result in *difference.
 */
constexpr bool mongerSignedSubtractOverflow64(int64_t lhs, int64_t rhs, int64_t* difference);
constexpr bool mongerUnsignedSubtractOverflow64(uint64_t lhs, uint64_t rhs, uint64_t* difference);


#ifdef _MSC_VER

// The SafeInt functions return true on success, false on overflow.

constexpr bool mongerSignedMultiplyOverflow64(int64_t lhs, int64_t rhs, int64_t* product) {
    return !SafeMultiply(lhs, rhs, *product);
}

constexpr bool mongerUnsignedMultiplyOverflow64(uint64_t lhs, uint64_t rhs, uint64_t* product) {
    return !SafeMultiply(lhs, rhs, *product);
}

constexpr bool mongerSignedAddOverflow64(int64_t lhs, int64_t rhs, int64_t* sum) {
    return !SafeAdd(lhs, rhs, *sum);
}

constexpr bool mongerUnsignedAddOverflow64(uint64_t lhs, uint64_t rhs, uint64_t* sum) {
    return !SafeAdd(lhs, rhs, *sum);
}

constexpr bool mongerSignedSubtractOverflow64(int64_t lhs, int64_t rhs, int64_t* difference) {
    return !SafeSubtract(lhs, rhs, *difference);
}

constexpr bool mongerUnsignedSubtractOverflow64(uint64_t lhs, uint64_t rhs, uint64_t* difference) {
    return !SafeSubtract(lhs, rhs, *difference);
}

#else

// On GCC and CLANG we can use __builtin functions to perform these calculations. These return true
// on overflow and false on success.

constexpr bool mongerSignedMultiplyOverflow64(long lhs, long rhs, long* product) {
    return __builtin_mul_overflow(lhs, rhs, product);
}

constexpr bool mongerSignedMultiplyOverflow64(long long lhs, long long rhs, long long* product) {
    return __builtin_mul_overflow(lhs, rhs, product);
}

constexpr bool mongerUnsignedMultiplyOverflow64(unsigned long lhs,
                                               unsigned long rhs,
                                               unsigned long* product) {
    return __builtin_mul_overflow(lhs, rhs, product);
}

constexpr bool mongerUnsignedMultiplyOverflow64(unsigned long long lhs,
                                               unsigned long long rhs,
                                               unsigned long long* product) {
    return __builtin_mul_overflow(lhs, rhs, product);
}

constexpr bool mongerSignedAddOverflow64(long lhs, long rhs, long* sum) {
    return __builtin_add_overflow(lhs, rhs, sum);
}

constexpr bool mongerSignedAddOverflow64(long long lhs, long long rhs, long long* sum) {
    return __builtin_add_overflow(lhs, rhs, sum);
}

constexpr bool mongerUnsignedAddOverflow64(unsigned long lhs,
                                          unsigned long rhs,
                                          unsigned long* sum) {
    return __builtin_add_overflow(lhs, rhs, sum);
}

constexpr bool mongerUnsignedAddOverflow64(unsigned long long lhs,
                                          unsigned long long rhs,
                                          unsigned long long* sum) {
    return __builtin_add_overflow(lhs, rhs, sum);
}

constexpr bool mongerSignedSubtractOverflow64(long lhs, long rhs, long* difference) {
    return __builtin_sub_overflow(lhs, rhs, difference);
}

constexpr bool mongerSignedSubtractOverflow64(long long lhs, long long rhs, long long* difference) {
    return __builtin_sub_overflow(lhs, rhs, difference);
}

constexpr bool mongerUnsignedSubtractOverflow64(unsigned long lhs,
                                               unsigned long rhs,
                                               unsigned long* sum) {
    return __builtin_sub_overflow(lhs, rhs, sum);
}

constexpr bool mongerUnsignedSubtractOverflow64(unsigned long long lhs,
                                               unsigned long long rhs,
                                               unsigned long long* sum) {
    return __builtin_sub_overflow(lhs, rhs, sum);
}

#endif

}  // namespace monger
