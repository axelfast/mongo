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

#include "monger/base/status.h"
#include "monger/bson/mutable/document.h"

namespace monger {

/**
 * Previously, there were multiple supported versions of the update language.
 */
enum class UpdateSemantics {
    // The update system introduced in v3.6, and is the only supported system. When a single update
    // adds multiple fields, those fields are added in lexicographic order by field name. This
    // system introduces support for arrayFilters and $[] syntax.
    kUpdateNode = 1,

    // Must be last.
    kNumUpdateSemantics
};

/** LogBuilder abstracts away some of the details of producing a properly constructed oplog
 *  update entry. It manages separate regions into which it accumulates $set and $unset
 *  operations, and distinguishes object replacement style oplog generation from
 *  $set/$unset style generation and prevents admixture.
 */
class LogBuilder {
public:
    static constexpr StringData kUpdateSemanticsFieldName = "$v"_sd;

    /** Construct a new LogBuilder. Log entries will be recorded as new children under the
     *  'logRoot' Element, which must be of type monger::Object and have no children.
     */
    inline LogBuilder(mutablebson::Element logRoot)
        : _logRoot(logRoot),
          _objectReplacementAccumulator(_logRoot),
          _setAccumulator(_logRoot.getDocument().end()),
          _unsetAccumulator(_setAccumulator),
          _updateSemantics(_setAccumulator) {
        dassert(logRoot.isType(monger::Object));
        dassert(!logRoot.hasChildren());
    }

    /** Return the Document to which the logging root belongs. */
    inline mutablebson::Document& getDocument() {
        return _logRoot.getDocument();
    }

    /** Add the given Element as a new entry in the '$set' section of the log. If a $set
     *  section does not yet exist, it will be created. If this LogBuilder is currently
     *  configured to contain an object replacement, the request to add to the $set section
     *  will return an Error.
     */
    Status addToSets(mutablebson::Element elt);

    /**
     * Convenience method which calls addToSets after
     * creating a new Element to wrap the SafeNum value.
     *
     * If any problem occurs then the operation will stop and return that error Status.
     */
    Status addToSets(StringData name, const SafeNum& val);

    /**
     * Convenience method which calls addToSets after
     * creating a new Element to wrap the old one.
     *
     * If any problem occurs then the operation will stop and return that error Status.
     */
    Status addToSetsWithNewFieldName(StringData name, const mutablebson::Element val);

    /**
     * Convenience method which calls addToSets after
     * creating a new Element to wrap the old one.
     *
     * If any problem occurs then the operation will stop and return that error Status.
     */
    Status addToSetsWithNewFieldName(StringData name, const BSONElement& val);

    /** Add the given path as a new entry in the '$unset' section of the log. If an
     *  '$unset' section does not yet exist, it will be created. If this LogBuilder is
     *  currently configured to contain an object replacement, the request to add to the
     *  $unset section will return an Error.
     */
    Status addToUnsets(StringData path);

    /**
     * Add a "$v" field to the log. Fails if there is already a "$v" field.
     */
    Status setUpdateSemantics(UpdateSemantics updateSemantics);

    /** Obtain, via the out parameter 'outElt', a pointer to the monger::Object type Element
     *  to which the components of an object replacement should be recorded. It is an error
     *  to call this if any Elements have been added by calling either addToSets or
     *  addToUnsets, and attempts to do so will return a non-OK Status. Similarly, if there
     *  is already object replacement data recorded for this log, the call will fail.
     */
    Status getReplacementObject(mutablebson::Element* outElt);

private:
    // Returns true if the object replacement accumulator is valid and has children, false
    // otherwise.
    inline bool hasObjectReplacement() const;

    inline Status addToSection(mutablebson::Element newElt,
                               mutablebson::Element* section,
                               const char* sectionName);

    mutablebson::Element _logRoot;
    mutablebson::Element _objectReplacementAccumulator;
    mutablebson::Element _setAccumulator;
    mutablebson::Element _unsetAccumulator;
    mutablebson::Element _updateSemantics;
};

}  // namespace monger
