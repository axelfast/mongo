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
#include <limits>
#include <memory>
#include <vector>

#include "monger/base/string_data.h"
#include "monger/db/update/modifier_node.h"
#include "monger/db/update/push_sorter.h"

namespace monger {

class PushNode final : public ModifierNode {
public:
    Status init(BSONElement modExpr, const boost::intrusive_ptr<ExpressionContext>& expCtx) final;

    std::unique_ptr<UpdateNode> clone() const final {
        return std::make_unique<PushNode>(*this);
    }

    void setCollator(const CollatorInterface* collator) final {
        if (_sort) {
            invariant(!_sort->collator);
            _sort->collator = collator;
        }
    }

    void acceptVisitor(UpdateNodeVisitor* visitor) final {
        visitor->visit(this);
    }

protected:
    ModifyResult updateExistingElement(mutablebson::Element* element,
                                       std::shared_ptr<FieldRef> elementPath) const final;
    void setValueForNewElement(mutablebson::Element* element) const final;
    void logUpdate(LogBuilder* logBuilder,
                   StringData pathTaken,
                   mutablebson::Element element,
                   ModifyResult modifyResult) const final;

    bool allowCreation() const final {
        return true;
    }


private:
    StringData operatorName() const final {
        return "$push";
    }

    BSONObj operatorValue() const final;

    // A helper for performPush().
    static ModifyResult insertElementsWithPosition(mutablebson::Element* array,
                                                   boost::optional<long long> position,
                                                   const std::vector<BSONElement>& valuesToPush);

    /**
     * Inserts the elements from '_valuesToPush' in the 'element' array using '_position' to
     * determine where to insert. This function also applies any '_slice' and or '_sort' that is
     * specified. The return value of this function will indicate to logUpdate() what kind of oplog
     * entries should be generated.
     *
     * Returns:
     *   - ModifyResult::kNoOp if '_valuesToPush' is empty and no slice or sort gets performed;
     *   - ModifyResult::kArrayAppendUpdate if the 'elements' array is initially non-empty, all
     *     inserted values are appended to the end, and no slice or sort gets performed; or
     *   - ModifyResult::kNormalUpdate if 'elements' is initially an empty array, values get
     *     inserted at the beginning or in the middle of the array, or a slice or sort gets
     *     performed.
     */
    ModifyResult performPush(mutablebson::Element* element, FieldRef* elementPath) const;

    static const StringData kEachClauseName;
    static const StringData kSliceClauseName;
    static const StringData kSortClauseName;
    static const StringData kPositionClauseName;

    std::vector<BSONElement> _valuesToPush;
    boost::optional<long long> _slice;
    boost::optional<long long> _position;
    boost::optional<PatternElementCmp> _sort;
};

}  // namespace monger
