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

#include "monger/util/hex.h"

#include <iomanip>
#include <sstream>
#include <string>

namespace monger {

template <typename T>
std::string integerToHexDef(T inInt) {
    if (!inInt)
        return "0";

    static const char hexchars[] = "0123456789ABCDEF";

    static const size_t outbufSize = sizeof(T) * 2 + 1;
    char outbuf[outbufSize];
    outbuf[outbufSize - 1] = '\0';

    char c;
    int lastSeenNumber = 0;
    for (int j = int(outbufSize) - 2; j >= 0; j--) {
        c = hexchars[inInt & 0xF];
        if (c != '0')
            lastSeenNumber = j;
        outbuf[j] = c;
        inInt = inInt >> 4;
    }
    char* bufPtr = outbuf;
    bufPtr += lastSeenNumber;

    return std::string(bufPtr);
}

template <>
std::string integerToHex<char>(char val) {
    return integerToHexDef(val);
}
template <>
std::string integerToHex<int>(int val) {
    return integerToHexDef(val);
}
template <>
std::string integerToHex<unsigned int>(unsigned int val) {
    return integerToHexDef(val);
}
template <>
std::string integerToHex<long>(long val) {
    return integerToHexDef(val);
}
template <>
std::string integerToHex<unsigned long>(unsigned long val) {
    return integerToHexDef(val);
}
template <>
std::string integerToHex<long long>(long long val) {
    return integerToHexDef(val);
}
template <>
std::string integerToHex<unsigned long long>(unsigned long long val) {
    return integerToHexDef(val);
}

std::string unsignedIntToFixedLengthHex(uint32_t val) {
    char buf[9];
    invariant(snprintf(buf, 9, "%08X", val) == 8);
    return std::string(buf, 8);
}

std::string hexdump(const char* data, unsigned len) {
    verify(len < 1000000);
    const unsigned char* p = (const unsigned char*)data;
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned i = 0; i < len; i++) {
        ss << std::setw(2) << static_cast<unsigned>(p[i]) << ' ';
    }
    std::string s = ss.str();
    return s;
}
}
