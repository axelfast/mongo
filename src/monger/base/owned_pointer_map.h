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

#include <map>


namespace monger {

/**
 * An std::map wrapper that deletes pointers within a vector on destruction.  The objects
 * referenced by the vector's pointers are 'owned' by an object of this class.
 * NOTE that an OwnedPointerMap<K,T,Compare> wraps an std::map<K,T*,Compare>.
 */
template <class K, class T, class Compare = std::less<K>>
class OwnedPointerMap {
    OwnedPointerMap(const OwnedPointerMap&) = delete;
    OwnedPointerMap& operator=(const OwnedPointerMap&) = delete;

public:
    typedef typename std::map<K, T*, Compare> MapType;

    OwnedPointerMap();
    ~OwnedPointerMap();

    /** Access the map. */
    const MapType& map() const {
        return _map;
    }
    MapType& mutableMap() {
        return _map;
    }

    void clear();

private:
    MapType _map;
};

template <class K, class T, class Compare>
OwnedPointerMap<K, T, Compare>::OwnedPointerMap() {}

template <class K, class T, class Compare>
OwnedPointerMap<K, T, Compare>::~OwnedPointerMap() {
    clear();
}

template <class K, class T, class Compare>
void OwnedPointerMap<K, T, Compare>::clear() {
    for (typename MapType::iterator i = _map.begin(); i != _map.end(); ++i) {
        delete i->second;
    }
    _map.clear();
}

}  // namespace monger
