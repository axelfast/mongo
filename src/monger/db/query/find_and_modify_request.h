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

#include "monger/db/jsobj.h"
#include "monger/db/namespace_string.h"
#include "monger/db/ops/write_ops_parsers.h"
#include "monger/db/pipeline/runtime_constants_gen.h"
#include "monger/db/write_concern_options.h"

namespace monger {

template <typename T>
class StatusWith;

/**
 * Represents the user-supplied options to the findAndModify command. Note that this
 * does not offer round trip preservation. For example, for the case where
 * output = parseBSON(input).toBSON(), 'output' is not guaranteed to be equal to 'input'.
 * However, the semantic meaning of 'output' will be the same with 'input'.
 *
 * The BSONObj members contained within this struct are owned objects.
 */
class FindAndModifyRequest {
public:
    static constexpr auto kLegacyCommandName = "findandmodify"_sd;
    static constexpr auto kCommandName = "findAndModify"_sd;

    /**
     * Creates a new instance of a 'update' type findAndModify request.
     */
    static FindAndModifyRequest makeUpdate(NamespaceString fullNs,
                                           BSONObj query,
                                           write_ops::UpdateModification update);

    /**
     * Creates a new instance of an 'remove' type findAndModify request.
     */
    static FindAndModifyRequest makeRemove(NamespaceString fullNs, BSONObj query);

    /**
     * Create a new instance of FindAndModifyRequest from a valid BSONObj.
     * Returns an error if the BSONObj is malformed.
     * Format:
     *
     * {
     *   findAndModify: <collection-name>,
     *   query: <document>,
     *   sort: <document>,
     *   collation: <document>,
     *   arrayFilters: <array>,
     *   remove: <boolean>,
     *   update: <document>,
     *   new: <boolean>,
     *   fields: <document>,
     *   upsert: <boolean>
     * }
     *
     * Note: does not parse the writeConcern field or the findAndModify field.
     */
    static StatusWith<FindAndModifyRequest> parseFromBSON(NamespaceString fullNs,
                                                          const BSONObj& cmdObj);

    /**
     * Serializes this object into a BSON representation. Fields that are not
     * set will not be part of the the serialized object. Passthrough fields
     * are appended.
     */
    BSONObj toBSON(const BSONObj& commandPassthroughFields) const;

    const NamespaceString& getNamespaceString() const;
    BSONObj getQuery() const;
    BSONObj getFields() const;
    const boost::optional<write_ops::UpdateModification>& getUpdate() const;
    BSONObj getSort() const;
    BSONObj getCollation() const;
    const std::vector<BSONObj>& getArrayFilters() const;
    bool shouldReturnNew() const;
    bool isUpsert() const;
    bool isRemove() const;
    bool getBypassDocumentValidation() const;

    // Not implemented. Use extractWriteConcern() to get the setting instead.
    WriteConcernOptions getWriteConcern() const;

    //
    // Setters for update type request only.
    //

    /**
    * Sets the filter to find a document.
    */
    void setQuery(BSONObj query);

    /**
    * Sets the update object that specifies how a document gets updated.
    */
    void setUpdateObj(BSONObj updateObj);

    /**
     * If shouldReturnNew is new, the findAndModify response should return the document
     * after the modification was applied if the query matched a document. Otherwise,
     * it will return the matched document before the modification.
     */
    void setShouldReturnNew(bool shouldReturnNew);

    /**
    * Sets a flag whether the statement performs an upsert.
    */
    void setUpsert(bool upsert);

    //
    // Setters for optional parameters
    //

    /**
     * Specifies the field to project on the matched document.
     */
    void setFieldProjection(BSONObj fields);

    /**
     * Sets the sort order for the query. In cases where the query yields multiple matches,
     * only the first document based on the sort order will be modified/removed.
     */
    void setSort(BSONObj sort);

    /**
     * Sets the collation for the query, which is used for all string comparisons.
     */
    void setCollation(BSONObj collation);

    /**
     * Sets the array filters for the update, which determine which array elements should be
     * modified.
     */
    void setArrayFilters(const std::vector<BSONObj>& arrayFilters);

    /**
     * Sets any constant values which may be required by the query and/or update.
     */
    void setRuntimeConstants(RuntimeConstants runtimeConstants) {
        _runtimeConstants = std::move(runtimeConstants);
    }

    /**
     * Returns the runtime constants associated with this findAndModify request, if present.
     */
    const boost::optional<RuntimeConstants>& getRuntimeConstants() const {
        return _runtimeConstants;
    }

    /**
     * Sets the write concern for this request.
     */
    void setWriteConcern(WriteConcernOptions writeConcern);

    void setBypassDocumentValidation(bool bypassDocumentValidation);

private:
    /**
     * Creates a new FindAndModifyRequest with the required fields.
     */
    FindAndModifyRequest(NamespaceString fullNs,
                         BSONObj query,
                         boost::optional<write_ops::UpdateModification> update);

    // Required fields
    const NamespaceString _ns;
    BSONObj _query;

    bool _isUpsert{false};
    boost::optional<BSONObj> _fieldProjection;
    boost::optional<BSONObj> _sort;
    boost::optional<BSONObj> _collation;
    boost::optional<std::vector<BSONObj>> _arrayFilters;
    boost::optional<RuntimeConstants> _runtimeConstants;
    bool _shouldReturnNew{false};
    boost::optional<WriteConcernOptions> _writeConcern;
    bool _bypassDocumentValidation{false};

    // Holds value when performing an update request and none when a remove request.
    boost::optional<write_ops::UpdateModification> _update;
};
}
