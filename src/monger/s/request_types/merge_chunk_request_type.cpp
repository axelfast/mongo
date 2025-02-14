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

#include "monger/platform/basic.h"

#include "monger/s/request_types/merge_chunk_request_type.h"

#include "monger/bson/bsonobjbuilder.h"
#include "monger/bson/util/bson_extract.h"

namespace monger {
namespace {

const char kConfigsvrMergeChunk[] = "_configsvrCommitChunkMerge";
const char kCollEpoch[] = "collEpoch";
const char kChunkBoundaries[] = "chunkBoundaries";
const char kShardName[] = "shard";
const char kValidAfter[] = "validAfter";
}  // namespace

MergeChunkRequest::MergeChunkRequest(NamespaceString nss,
                                     std::string shardName,
                                     OID epoch,
                                     std::vector<BSONObj> chunkBoundaries,
                                     boost::optional<Timestamp> validAfter)
    : _nss(std::move(nss)),
      _epoch(std::move(epoch)),
      _chunkBoundaries(std::move(chunkBoundaries)),
      _shardName(std::move(shardName)),
      _validAfter(validAfter) {}

StatusWith<MergeChunkRequest> MergeChunkRequest::parseFromConfigCommand(const BSONObj& cmdObj) {
    std::string ns;
    {
        auto parseNamespaceStatus = bsonExtractStringField(cmdObj, kConfigsvrMergeChunk, &ns);
        if (!parseNamespaceStatus.isOK()) {
            return parseNamespaceStatus;
        }
    }

    NamespaceString nss(ns);
    if (!nss.isValid()) {
        return {ErrorCodes::InvalidNamespace,
                str::stream() << "invalid namespace '" << nss.ns() << "' specified for request"};
    }

    OID epoch;
    {
        auto parseEpochStatus = bsonExtractOIDField(cmdObj, kCollEpoch, &epoch);
        if (!parseEpochStatus.isOK()) {
            return parseEpochStatus;
        }
    }

    std::vector<BSONObj> chunkBoundaries;
    {
        BSONElement chunkBoundariesElem;
        auto chunkBoundariesElemStatus =
            bsonExtractTypedField(cmdObj, kChunkBoundaries, monger::Array, &chunkBoundariesElem);

        if (!chunkBoundariesElemStatus.isOK()) {
            return chunkBoundariesElemStatus;
        }
        BSONObjIterator it(chunkBoundariesElem.Obj());
        while (it.more()) {
            chunkBoundaries.push_back(it.next().Obj().getOwned());
        }

        if (chunkBoundaries.size() < 3) {
            return {ErrorCodes::InvalidOptions,
                    "need to provide at least three chunk boundaries for the chunks to be merged"};
        }
    }

    std::string shardName;
    {
        auto parseShardNameStatus = bsonExtractStringField(cmdObj, kShardName, &shardName);
        if (!parseShardNameStatus.isOK()) {
            return parseShardNameStatus;
        }
    }

    boost::optional<Timestamp> validAfter = boost::none;
    {
        Timestamp ts{0};
        auto status = bsonExtractTimestampField(cmdObj, kValidAfter, &ts);
        if (!status.isOK() && status != ErrorCodes::NoSuchKey) {
            return status;
        }

        if (status.isOK()) {
            validAfter = ts;
        }
    }

    return MergeChunkRequest(std::move(nss),
                             std::move(shardName),
                             std::move(epoch),
                             std::move(chunkBoundaries),
                             validAfter);
}

BSONObj MergeChunkRequest::toConfigCommandBSON(const BSONObj& writeConcern) {
    BSONObjBuilder cmdBuilder;
    appendAsConfigCommand(&cmdBuilder);

    // Tack on passed-in writeConcern
    cmdBuilder.appendElements(writeConcern);

    return cmdBuilder.obj();
}

void MergeChunkRequest::appendAsConfigCommand(BSONObjBuilder* cmdBuilder) {
    cmdBuilder->append(kConfigsvrMergeChunk, _nss.ns());
    cmdBuilder->append(kCollEpoch, _epoch);
    {
        BSONArrayBuilder chunkBoundariesArray(cmdBuilder->subarrayStart(kChunkBoundaries));
        for (const auto& chunkBoundary : _chunkBoundaries) {
            chunkBoundariesArray.append(chunkBoundary);
        }
    }
    cmdBuilder->append(kShardName, _shardName);
    invariant(_validAfter.is_initialized());
    cmdBuilder->append(kValidAfter, _validAfter.get());
}

}  // namespace monger
