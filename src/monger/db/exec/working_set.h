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

#include "boost/optional.hpp"
#include <vector>

#include "monger/db/jsobj.h"
#include "monger/db/record_id.h"
#include "monger/db/storage/snapshot.h"
#include "monger/stdx/unordered_set.h"

namespace monger {

class IndexAccessMethod;
class WorkingSetMember;

typedef size_t WorkingSetID;

/**
 * All data in use by a query.  Data is passed through the stage tree by referencing the ID of
 * an element of the working set.  Stages can add elements to the working set, delete elements
 * from the working set, or mutate elements in the working set.
 */
class WorkingSet {
    WorkingSet(const WorkingSet&) = delete;
    WorkingSet& operator=(const WorkingSet&) = delete;

public:
    static const WorkingSetID INVALID_ID = WorkingSetID(-1);

    WorkingSet();
    ~WorkingSet();

    /**
     * Allocate a new query result and return the ID used to get and free it.
     */
    WorkingSetID allocate();

    /**
     * Get the i-th mutable query result. The pointer will be valid for this id until freed.
     * Do not delete the returned pointer as the WorkingSet retains ownership. Call free() to
     * release it.
     */
    WorkingSetMember* get(WorkingSetID i) const {
        dassert(i < _data.size());              // ID has been allocated.
        dassert(_data[i].nextFreeOrSelf == i);  // ID currently in use.
        return _data[i].member;
    }

    /**
     * Returns true if WorkingSetMember with id 'i' is free.
     */
    bool isFree(WorkingSetID i) const {
        return _data[i].nextFreeOrSelf != i;
    }

    /**
     * Deallocate the i-th query result and release its resources.
     */
    void free(WorkingSetID i);

    /**
     * Removes and deallocates all members of this working set.
     */
    void clear();

    //
    // WorkingSetMember state transitions
    //

    void transitionToRecordIdAndIdx(WorkingSetID id);
    void transitionToRecordIdAndObj(WorkingSetID id);
    void transitionToOwnedObj(WorkingSetID id);

    /**
     * Returns the list of working set ids that have transitioned into the RID_AND_IDX state since
     * the last yield. The members corresponding to these ids may have since transitioned to a
     * different state or been freed, so these cases must be handled by the caller. The list may
     * also contain duplicates.
     *
     * Execution stages are *not* responsible for managing this list, as working set ids are added
     * to the set automatically by WorkingSet::transitionToRecordIdAndIdx().
     *
     * As a side effect, calling this method clears the list of flagged yield sensitive ids kept by
     * the working set.
     */
    std::vector<WorkingSetID> getAndClearYieldSensitiveIds();

private:
    struct MemberHolder {
        MemberHolder();
        ~MemberHolder();

        // Free list link if freed. Points to self if in use.
        WorkingSetID nextFreeOrSelf;

        // Owning pointer
        WorkingSetMember* member;
    };

    // All WorkingSetIDs are indexes into this, except for INVALID_ID.
    // Elements are added to _freeList rather than removed when freed.
    std::vector<MemberHolder> _data;

    // Index into _data, forming a linked-list using MemberHolder::nextFreeOrSelf as the next
    // link. INVALID_ID is the list terminator since 0 is a valid index.
    // If _freeList == INVALID_ID, the free list is empty and all elements in _data are in use.
    WorkingSetID _freeList;

    // Contains ids of WSMs that may need to be adjusted when we next yield.
    std::vector<WorkingSetID> _yieldSensitiveIds;
};

/**
 * The key data extracted from an index.  Keeps track of both the key (currently a BSONObj) and
 * the index that provided the key.  The index key pattern is required to correctly interpret
 * the key.
 */
struct IndexKeyDatum {
    IndexKeyDatum(const BSONObj& keyPattern, const BSONObj& key, const IndexAccessMethod* index)
        : indexKeyPattern(keyPattern), keyData(key), index(index) {}

    /**
     * getFieldDotted produces the field with the provided name based on index keyData. The return
     * object is populated if the element is in a provided index key.  Returns none otherwise.
     * Returning none indicates a query planning error.
     */
    static boost::optional<BSONElement> getFieldDotted(const std::vector<IndexKeyDatum>& keyData,
                                                       const std::string& field) {
        for (size_t i = 0; i < keyData.size(); ++i) {
            BSONObjIterator keyPatternIt(keyData[i].indexKeyPattern);
            BSONObjIterator keyDataIt(keyData[i].keyData);

            while (keyPatternIt.more()) {
                BSONElement keyPatternElt = keyPatternIt.next();
                verify(keyDataIt.more());
                BSONElement keyDataElt = keyDataIt.next();

                if (field == keyPatternElt.fieldName())
                    return boost::make_optional(keyDataElt);
            }
        }
        return boost::none;
    }

    // This is not owned and points into the IndexDescriptor's data.
    BSONObj indexKeyPattern;

    // This is the BSONObj for the key that we put into the index.  Owned by us.
    BSONObj keyData;

    const IndexAccessMethod* index;
};

/**
 * What types of computed data can we have?
 */
enum WorkingSetComputedDataType {
    // What's the score of the document retrieved from a $text query?
    WSM_COMPUTED_TEXT_SCORE = 0,

    // What's the distance from a geoNear query point to the document?
    WSM_COMPUTED_GEO_DISTANCE = 1,

    // The index key used to retrieve the document, for returnKey query option.
    WSM_INDEX_KEY = 2,

    // What point (of several possible points) was used to compute the distance to the document
    // via geoNear?
    WSM_GEO_NEAR_POINT = 3,

    // Comparison key for sorting.
    WSM_SORT_KEY = 4,

    // Must be last.
    WSM_COMPUTED_NUM_TYPES,
};

/**
 * Data that is a computed function of a WSM.
 */
class WorkingSetComputedData {
    WorkingSetComputedData(const WorkingSetComputedData&) = delete;
    WorkingSetComputedData& operator=(const WorkingSetComputedData&) = delete;

public:
    WorkingSetComputedData(const WorkingSetComputedDataType type) : _type(type) {}
    virtual ~WorkingSetComputedData() {}

    WorkingSetComputedDataType type() const {
        return _type;
    }

    virtual WorkingSetComputedData* clone() const = 0;

private:
    WorkingSetComputedDataType _type;
};

/**
 * The type of the data passed between query stages.  In particular:
 *
 * Index scan stages return a WorkingSetMember in the RID_AND_IDX state.
 *
 * Collection scan stages return a WorkingSetMember in the RID_AND_OBJ state.
 *
 * A WorkingSetMember may have any of the data above.
 */
class WorkingSetMember {
    WorkingSetMember(const WorkingSetMember&) = delete;
    WorkingSetMember& operator=(const WorkingSetMember&) = delete;

public:
    WorkingSetMember();
    ~WorkingSetMember();

    /**
     * Reset to an "empty" state.
     */
    void clear();

    enum MemberState {
        // Initial state.
        INVALID,

        // Data is from 1 or more indices.
        RID_AND_IDX,

        // Data is from a collection scan, or data is from an index scan and was fetched. The
        // BSONObj might be owned or unowned.
        RID_AND_OBJ,

        // The WSM doesn't correspond to an on-disk document anymore (e.g. is a computed
        // expression). Since it doesn't correspond to a stored document, a WSM in this state has an
        // owned BSONObj, but no record id.
        OWNED_OBJ,
    };

    //
    // Member state and state transitions
    //

    MemberState getState() const;

    void transitionToOwnedObj();

    //
    // Core attributes
    //

    RecordId recordId;
    Snapshotted<BSONObj> obj;
    std::vector<IndexKeyDatum> keyData;

    // True if this WSM has survived a yield in RID_AND_IDX state.
    // TODO consider replacing by tracking SnapshotIds for IndexKeyDatums.
    bool isSuspicious = false;

    bool hasRecordId() const;
    bool hasObj() const;
    bool hasOwnedObj() const;

    /**
     * Ensures that 'obj' of a WSM in the RID_AND_OBJ state is owned BSON. It is a no-op if the WSM
     * is in a different state or if 'obj' is already owned.
     *
     * It is illegal for unowned BSON to survive a yield, so this must be called on any working set
     * members which may stay alive across yield points.
     */
    void makeObjOwnedIfNeeded();

    //
    // Computed data
    //

    bool hasComputed(const WorkingSetComputedDataType type) const;
    const WorkingSetComputedData* getComputed(const WorkingSetComputedDataType type) const;
    void addComputed(WorkingSetComputedData* data);

    /**
     * getFieldDotted uses its state (obj or index data) to produce the field with the provided
     * name.
     *
     * Returns true if there is the element is in an index key or in an (owned or unowned)
     * object.  *out is set to the element if so.
     *
     * Returns false otherwise.  Returning false indicates a query planning error.
     */
    bool getFieldDotted(const std::string& field, BSONElement* out) const;

    /**
     * Returns expected memory usage of working set member.
     */
    size_t getMemUsage() const;

private:
    friend class WorkingSet;

    MemberState _state = WorkingSetMember::INVALID;

    std::unique_ptr<WorkingSetComputedData> _computed[WSM_COMPUTED_NUM_TYPES];
};

}  // namespace monger
