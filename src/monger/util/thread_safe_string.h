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

#include <cstring>
#include <iosfwd>
#include <string>

#include "monger/base/string_data.h"

namespace monger {

/**
 * this is a thread safe string
 * you will never get a bad pointer, though data may be mungedd
 */
class ThreadSafeString {
    ThreadSafeString(const ThreadSafeString&) = delete;
    ThreadSafeString& operator=(const ThreadSafeString&) = delete;

public:
    ThreadSafeString(size_t size = 256) : _size(size), _buf(new char[size]) {
        memset(_buf, '\0', _size);
    }

    ~ThreadSafeString() {
        delete[] _buf;
    }

    std::string toString() const {
        return _buf;
    }

    ThreadSafeString& operator=(StringData str) {
        size_t s = str.size();
        if (s >= _size - 2)
            s = _size - 2;
        strncpy(_buf, str.rawData(), s);
        _buf[s] = '\0';
        return *this;
    }

    bool empty() const {
        return _buf[0] == '\0';
    }

private:
    const size_t _size;
    char* const _buf;
};

std::ostream& operator<<(std::ostream& s, const ThreadSafeString& o);

}  // namespace monger
