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

#include <memory>

#include "monger/base/string_data.h"
#include "monger/db/update/modifier_node.h"

namespace monger {

/**
 * Represents the application of a $unset to the value at the end of a path.
 */
class UnsetNode : public ModifierNode {
public:
    Status init(BSONElement modExpr, const boost::intrusive_ptr<ExpressionContext>& expCtx) final;

    std::unique_ptr<UpdateNode> clone() const final {
        return std::make_unique<UnsetNode>(*this);
    }

    void setCollator(const CollatorInterface* collator) final {}

    ModifyResult updateExistingElement(mutablebson::Element* element,
                                       std::shared_ptr<FieldRef> elementPath) const final;

    void validateUpdate(mutablebson::ConstElement updatedElement,
                        mutablebson::ConstElement leftSibling,
                        mutablebson::ConstElement rightSibling,
                        std::uint32_t recursionLevel,
                        ModifyResult modifyResult) const final;

    void logUpdate(LogBuilder* logBuilder,
                   StringData pathTaken,
                   mutablebson::Element element,
                   ModifyResult modifyResult) const final;

    bool allowNonViablePath() const final {
        return true;
    }

    void acceptVisitor(UpdateNodeVisitor* visitor) final {
        visitor->visit(this);
    }

private:
    StringData operatorName() const final {
        return "$unset";
    }

    BSONObj operatorValue() const final {
        return BSON("" << 1);
    }
};

}  // namespace monger
