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

#include "monger/bson/bsonobj.h"

namespace monger {
class BSONObjBuilder;
class Status;

/**
 * Utility functions for converting aggregation responses into other CRUD command responses.
 */
class ViewResponseFormatter {

public:
    static const char kCountField[];
    static const char kDistinctField[];
    static const char kOkField[];

    explicit ViewResponseFormatter(BSONObj aggregationResponse);

    /**
     * Appends fields to 'resultBuilder' as if '_response' were a response from the count command.
     *
     * If '_response' is not a valid cursor-based response from the aggregation command, a non-OK
     * status is returned and 'resultBuilder' will not be modified.
     */
    Status appendAsCountResponse(BSONObjBuilder* resultBuilder);

    /**
     * Appends fields to 'resultBuilder' as if '_response' were a response from the distinct
     * command.
     *
     * If '_response' is not a valid cursor-based response from the aggregation command, a non-OK
     * status is returned and 'resultBuilder' will not be modified.
     */
    Status appendAsDistinctResponse(BSONObjBuilder* resultBuilder);

private:
    BSONObj _response;
};
}  // namespace monger
