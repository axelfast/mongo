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

#include <boost/optional.hpp>
#include <string>

#include "monger/base/status.h"
#include "monger/base/status_with.h"
#include "monger/db/clientcursor.h"
#include "monger/db/namespace_string.h"
#include "monger/util/time_support.h"

namespace monger {

struct GetMoreRequest {
    static const char kGetMoreCommandName[];

    /**
     * Construct an empty request.
     */
    GetMoreRequest();

    /**
     * Construct from values for each field.
     */
    GetMoreRequest(NamespaceString namespaceString,
                   CursorId id,
                   boost::optional<std::int64_t> sizeOfBatch,
                   boost::optional<Milliseconds> awaitDataTimeout,
                   boost::optional<long long> term,
                   boost::optional<repl::OpTime> lastKnownCommittedOpTime);

    /**
     * Construct a GetMoreRequest from the command specification and db name.
     */
    static StatusWith<GetMoreRequest> parseFromBSON(const std::string& dbname,
                                                    const BSONObj& cmdObj);

    /**
     * Serializes this object into a BSON representation. Fields that are not set will not be
     * part of the the serialized object.
     */
    BSONObj toBSON() const;

    const NamespaceString nss;
    const CursorId cursorid;

    // The batch size is optional. If not provided, we will put as many documents into the batch
    // as fit within the byte limit.
    const boost::optional<std::int64_t> batchSize;

    // The number of milliseconds for which a getMore on a tailable, awaitData query should block.
    const boost::optional<Milliseconds> awaitDataTimeout;

    // Only internal queries from replication will typically have a term.
    const boost::optional<long long> term;

    // Only internal queries from replication will have a last known committed optime.
    const boost::optional<repl::OpTime> lastKnownCommittedOpTime;

private:
    /**
     * Returns a non-OK status if there are semantic errors in the parsed request
     * (e.g. a negative batchSize).
     */
    Status isValid() const;
};

}  // namespace monger
