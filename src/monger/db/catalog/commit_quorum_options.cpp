/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include "monger/platform/basic.h"

#include "monger/db/catalog/commit_quorum_options.h"

#include "monger/base/status.h"
#include "monger/base/string_data.h"
#include "monger/bson/util/bson_extract.h"
#include "monger/db/field_parser.h"
#include "monger/util/str.h"

namespace monger {

const StringData CommitQuorumOptions::kCommitQuorumField = "commitQuorum"_sd;
const char CommitQuorumOptions::kMajority[] = "majority";

const BSONObj CommitQuorumOptions::Majority(BSON(kCommitQuorumField
                                                 << CommitQuorumOptions::kMajority));

CommitQuorumOptions::CommitQuorumOptions(int numNodesOpts) {
    reset();
    numNodes = numNodesOpts;
    invariant(numNodes >= 0);
}

CommitQuorumOptions::CommitQuorumOptions(const std::string& modeOpts) {
    reset();
    mode = modeOpts;
    invariant(!mode.empty());
}

Status CommitQuorumOptions::parse(const BSONElement& commitQuorumElement) {
    reset();

    if (commitQuorumElement.isNumber()) {
        numNodes = commitQuorumElement.numberInt();
    } else if (commitQuorumElement.type() == String) {
        mode = commitQuorumElement.valuestrsafe();
    } else {
        return Status(ErrorCodes::FailedToParse, "commitQuorum has to be a number or a string");
    }

    return Status::OK();
}

CommitQuorumOptions CommitQuorumOptions::deserializerForIDL(
    const BSONElement& commitQuorumElement) {
    CommitQuorumOptions commitQuorumOptions;
    uassertStatusOK(commitQuorumOptions.parse(commitQuorumElement));
    return commitQuorumOptions;
}

BSONObj CommitQuorumOptions::toBSON() const {
    BSONObjBuilder builder;
    append(kCommitQuorumField, &builder);
    return builder.obj();
}

void CommitQuorumOptions::append(StringData fieldName, BSONObjBuilder* builder) const {
    if (mode.empty()) {
        builder->append(kCommitQuorumField, numNodes);
    } else {
        builder->append(kCommitQuorumField, mode);
    }
}

}  // namespace monger
