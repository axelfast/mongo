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

#include "monger/base/error_extra_info.h"
#include "monger/bson/bsonobj.h"
#include "monger/bson/bsonobjbuilder.h"

namespace monger {

/**
 * Represents an error returned from the storage engine when an attempt to insert a
 * key into a unique index fails because the same key already exists.
 */
class DuplicateKeyErrorInfo final : public ErrorExtraInfo {
public:
    static constexpr auto code = ErrorCodes::DuplicateKey;

    static std::shared_ptr<const ErrorExtraInfo> parse(const BSONObj&);

    explicit DuplicateKeyErrorInfo(const BSONObj& keyPattern, const BSONObj& keyValue)
        : _keyPattern(keyPattern.getOwned()), _keyValue(keyValue.getOwned()) {}

    void serialize(BSONObjBuilder* bob) const override;

    BSONObj toBSON() const {
        BSONObjBuilder bob;
        serialize(&bob);
        return bob.obj();
    }

    const BSONObj& getKeyPattern() const {
        return _keyPattern;
    }

    const BSONObj& getDuplicatedKeyValue() const {
        return _keyValue;
    }

private:
    BSONObj _keyPattern;
    BSONObj _keyValue;
};

}  // namespace monger
