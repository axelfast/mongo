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

#include <vector>

#include "monger/bson/bsonobj.h"
#include "monger/db/pipeline/monger_process_interface.h"

namespace monger {

/**
 * MongerProcessCommon provides base implementations of any MongerProcessInterface methods whose code
 * is largely identical on mongerD and mongerS.
 */
class MongerProcessCommon : public MongerProcessInterface {
public:
    virtual ~MongerProcessCommon() = default;

    /**
     * Returns true if the field names of 'keyPattern' are exactly those in 'uniqueKeyPaths', and
     * each of the elements of 'keyPattern' is numeric, i.e. not "text", "$**", or any other special
     * type of index.
     */
    static bool keyPatternNamesExactPaths(const BSONObj& keyPattern,
                                          const std::set<FieldPath>& uniqueKeyPaths);

    std::vector<BSONObj> getCurrentOps(const boost::intrusive_ptr<ExpressionContext>& expCtx,
                                       CurrentOpConnectionsMode connMode,
                                       CurrentOpSessionsMode sessionMode,
                                       CurrentOpUserMode userMode,
                                       CurrentOpTruncateMode truncateMode,
                                       CurrentOpCursorMode cursorMode) const final;

    virtual std::vector<FieldPath> collectDocumentKeyFieldsActingAsRouter(
        OperationContext*, const NamespaceString&) const override;

    boost::optional<ChunkVersion> refreshAndGetCollectionVersion(
        const boost::intrusive_ptr<ExpressionContext>& expCtx,
        const NamespaceString& nss) const final;

protected:
    /**
     * Converts the fields from a ShardKeyPattern to a vector of FieldPaths, including the _id if
     * it's not already in 'keyPatternFields'.
     */
    std::vector<FieldPath> _shardKeyToDocumentKeyFields(
        const std::vector<std::unique_ptr<FieldRef>>& keyPatternFields) const;

    /**
     * Returns a BSONObj representing a report of the operation which is currently being
     * executed by the supplied client. This method is called by the getCurrentOps method of
     * MongerProcessCommon to delegate to the mongerS- or mongerD- specific implementation.
     */
    virtual BSONObj _reportCurrentOpForClient(OperationContext* opCtx,
                                              Client* client,
                                              CurrentOpTruncateMode truncateOps) const = 0;

    /**
     * Iterates through all entries in the local SessionCatalog, and adds an entry to the 'ops'
     * vector for each idle session that has stashed its transaction locks while sleeping.
     */
    virtual void _reportCurrentOpsForIdleSessions(OperationContext* opCtx,
                                                  CurrentOpUserMode userMode,
                                                  std::vector<BSONObj>* ops) const = 0;

    /**
     * Converts an array of field names into a set of FieldPath. Throws if 'fields' contains
     * duplicate elements.
     */
    std::set<FieldPath> _convertToFieldPaths(const std::vector<std::string>& fields) const;
};

}  // namespace monger
