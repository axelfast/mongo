/**
 *    Copyright (C) 2018-present MongerDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongerDB, Inc.
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

#include "monger/db/logical_clock.h"
#include "monger/db/service_context.h"
#include "monger/db/service_context_test_fixture.h"
#include "monger/db/update/update_node.h"
#include "monger/unittest/unittest.h"

namespace monger {

class UpdateNodeTest : public ServiceContextTest {
public:
    ~UpdateNodeTest() override = default;

protected:
    void setUp() override {
        resetApplyParams();

        // Set up the logical clock needed by CurrentDateNode and ObjectReplaceExecutor.
        auto service = monger::getGlobalServiceContext();
        auto logicalClock = std::make_unique<monger::LogicalClock>(service);
        monger::LogicalClock::set(service, std::move(logicalClock));
    }

    void resetApplyParams() {
        _immutablePathsVector.clear();
        _immutablePaths.clear();
        _pathToCreate = std::make_shared<FieldRef>();
        _pathTaken = std::make_shared<FieldRef>();
        _matchedField = StringData();
        _insert = false;
        _fromOplogApplication = false;
        _validateForStorage = true;
        _indexData.reset();
        _logDoc.reset();
        _logBuilder = std::make_unique<LogBuilder>(_logDoc.root());
        _modifiedPaths.clear();
    }

    UpdateExecutor::ApplyParams getApplyParams(mutablebson::Element element) {
        UpdateExecutor::ApplyParams applyParams(element, _immutablePaths);
        applyParams.matchedField = _matchedField;
        applyParams.insert = _insert;
        applyParams.fromOplogApplication = _fromOplogApplication;
        applyParams.validateForStorage = _validateForStorage;
        applyParams.indexData = _indexData.get();
        applyParams.logBuilder = _logBuilder.get();
        applyParams.modifiedPaths = &_modifiedPaths;
        return applyParams;
    }

    UpdateNode::UpdateNodeApplyParams getUpdateNodeApplyParams() {
        UpdateNode::UpdateNodeApplyParams applyParams;
        applyParams.pathToCreate = _pathToCreate;
        applyParams.pathTaken = _pathTaken;
        return applyParams;
    }

    void addImmutablePath(StringData path) {
        auto fieldRef = std::make_unique<FieldRef>(path);
        _immutablePathsVector.push_back(std::move(fieldRef));
        _immutablePaths.insert(_immutablePathsVector.back().get());
    }

    void setPathToCreate(StringData path) {
        _pathToCreate->clear();
        _pathToCreate->parse(path);
    }

    void setPathTaken(StringData path) {
        _pathTaken->clear();
        _pathTaken->parse(path);
    }

    void setMatchedField(StringData matchedField) {
        _matchedField = matchedField;
    }

    void setInsert(bool insert) {
        _insert = insert;
    }

    void setFromOplogApplication(bool fromOplogApplication) {
        _fromOplogApplication = fromOplogApplication;
    }

    void setValidateForStorage(bool validateForStorage) {
        _validateForStorage = validateForStorage;
    }

    void addIndexedPath(StringData path) {
        if (!_indexData) {
            _indexData = std::make_unique<UpdateIndexData>();
        }
        _indexData->addPath(FieldRef(path));
    }

    void setLogBuilderToNull() {
        _logBuilder.reset();
    }

    const mutablebson::Document& getLogDoc() {
        return _logDoc;
    }

    std::string getModifiedPaths() {
        return _modifiedPaths.toString();
    }

private:
    std::vector<std::unique_ptr<FieldRef>> _immutablePathsVector;
    FieldRefSet _immutablePaths;
    std::shared_ptr<FieldRef> _pathToCreate;
    std::shared_ptr<FieldRef> _pathTaken;
    StringData _matchedField;
    bool _insert;
    bool _fromOplogApplication;
    bool _validateForStorage;
    std::unique_ptr<UpdateIndexData> _indexData;
    mutablebson::Document _logDoc;
    std::unique_ptr<LogBuilder> _logBuilder;
    FieldRefSetWithStorage _modifiedPaths;
};

}  // namespace monger
