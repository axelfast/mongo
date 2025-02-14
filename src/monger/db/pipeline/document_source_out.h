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

#include "monger/db/pipeline/document_source_writer.h"

namespace monger {
/**
 * Implementation for the $out aggregation stage.
 */
class DocumentSourceOut final : public DocumentSourceWriter<BSONObj> {
public:
    static constexpr StringData kStageName = "$out"_sd;

    /**
     * A "lite parsed" $out stage is similar to other stages involving foreign collections except in
     * some cases the foreign collection is allowed to be sharded.
     */
    class LiteParsed final : public LiteParsedDocumentSourceForeignCollections {
    public:
        using LiteParsedDocumentSourceForeignCollections::
            LiteParsedDocumentSourceForeignCollections;


        static std::unique_ptr<LiteParsed> parse(const AggregationRequest& request,
                                                 const BSONElement& spec);

        bool allowShardedForeignCollection(NamespaceString nss) const final {
            return _foreignNssSet.find(nss) == _foreignNssSet.end();
        }

        bool allowedToPassthroughFromMongers() const final {
            return false;
        }
    };

    ~DocumentSourceOut() override;

    const char* getSourceName() const final override {
        return kStageName.rawData();
    }

    StageConstraints constraints(Pipeline::SplitState pipeState) const final override {
        return {StreamType::kStreaming,
                PositionRequirement::kLast,
                HostTypeRequirement::kPrimaryShard,
                DiskUseRequirement::kWritesPersistentData,
                FacetRequirement::kNotAllowed,
                TransactionRequirement::kNotAllowed,
                LookupRequirement::kNotAllowed};
    }

    Value serialize(
        boost::optional<ExplainOptions::Verbosity> explain = boost::none) const final override;

    /**
     * Creates a new $out stage from the given arguments.
     */
    static boost::intrusive_ptr<DocumentSource> create(
        NamespaceString outputNs, const boost::intrusive_ptr<ExpressionContext>& expCtx);

    /**
     * Parses a $out stage from the user-supplied BSON.
     */
    static boost::intrusive_ptr<DocumentSource> createFromBson(
        BSONElement elem, const boost::intrusive_ptr<ExpressionContext>& pExpCtx);

private:
    DocumentSourceOut(NamespaceString outputNs,
                      const boost::intrusive_ptr<ExpressionContext>& expCtx)
        : DocumentSourceWriter(std::move(outputNs), expCtx) {}

    void initialize() override;

    void finalize() override;

    void spill(BatchedObjects&& batch) override {
        DocumentSourceWriteBlock writeBlock(pExpCtx->opCtx);

        auto targetEpoch = boost::none;
        uassertStatusOK(pExpCtx->mongerProcessInterface->insert(
            pExpCtx, _tempNs, std::move(batch), _writeConcern, targetEpoch));
    }

    std::pair<BSONObj, int> makeBatchObject(Document&& doc) const override {
        auto obj = doc.toBson();
        return {obj, obj.objsize()};
    }

    void waitWhileFailPointEnabled() override;

    // Holds on to the original collection options and index specs so we can check they didn't
    // change during computation.
    BSONObj _originalOutOptions;
    std::list<BSONObj> _originalIndexes;

    // The temporary namespace for the $out writes.
    NamespaceString _tempNs;
};

}  // namespace monger
