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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kDefault

#include "monger/platform/basic.h"

#include "monger/db/views/view_catalog.h"

#include <memory>
#include <string>

#include "monger/base/status_with.h"
#include "monger/base/string_data.h"
#include "monger/bson/util/builder.h"
#include "monger/db/catalog/database.h"
#include "monger/db/commands/feature_compatibility_version_command_parser.h"
#include "monger/db/namespace_string.h"
#include "monger/db/operation_context.h"
#include "monger/db/pipeline/aggregation_request.h"
#include "monger/db/pipeline/document_source.h"
#include "monger/db/pipeline/expression_context.h"
#include "monger/db/pipeline/lite_parsed_pipeline.h"
#include "monger/db/pipeline/pipeline.h"
#include "monger/db/pipeline/stub_monger_process_interface.h"
#include "monger/db/query/collation/collator_factory_interface.h"
#include "monger/db/storage/recovery_unit.h"
#include "monger/db/views/resolved_view.h"
#include "monger/db/views/view.h"
#include "monger/db/views/view_graph.h"
#include "monger/util/fail_point_service.h"
#include "monger/util/log.h"

namespace monger {

namespace {
auto getViewCatalog = Database::declareDecoration<std::unique_ptr<ViewCatalog>>();

StatusWith<std::unique_ptr<CollatorInterface>> parseCollator(OperationContext* opCtx,
                                                             BSONObj collationSpec) {
    // If 'collationSpec' is empty, return the null collator, which represents the "simple"
    // collation.
    if (collationSpec.isEmpty()) {
        return {nullptr};
    }
    return CollatorFactoryInterface::get(opCtx->getServiceContext())->makeFromBSON(collationSpec);
}
}  // namespace

ViewCatalog* ViewCatalog::get(const Database* db) {
    return getViewCatalog(db).get();
}

void ViewCatalog::set(Database* db, std::unique_ptr<ViewCatalog> catalog) {
    getViewCatalog(db) = std::move(catalog);
}

Status ViewCatalog::reloadIfNeeded(OperationContext* opCtx) {
    Lock::CollectionLock systemViewsLock(
        opCtx,
        NamespaceString(_durable->getName(), NamespaceString::kSystemDotViewsCollectionName),
        MODE_IS);
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    return _reloadIfNeeded(lk, opCtx, ViewCatalogLookupBehavior::kValidateDurableViews);
}

Status ViewCatalog::_reloadIfNeeded(WithLock lk,
                                    OperationContext* opCtx,
                                    ViewCatalogLookupBehavior lookupBehavior) {
    if (_valid.load())
        return Status::OK();

    LOG(1) << "reloading view catalog for database " << _durable->getName();

    // Need to reload, first clear our cache.
    _viewMap.clear();

    auto reloadCallback = [&](const BSONObj& view) -> Status {
        BSONObj collationSpec = view.hasField("collation") ? view["collation"].Obj() : BSONObj();
        auto collator = parseCollator(opCtx, collationSpec);
        if (!collator.isOK()) {
            return collator.getStatus();
        }

        NamespaceString viewName(view["_id"].str());

        auto pipeline = view["pipeline"].Obj();
        for (auto&& stage : pipeline) {
            if (BSONType::Object != stage.type()) {
                return Status(ErrorCodes::InvalidViewDefinition,
                              str::stream() << "View 'pipeline' entries must be objects, but "
                                            << viewName.toString()
                                            << " has a pipeline element of type "
                                            << stage.type());
            }
        }

        _viewMap[viewName.ns()] = std::make_shared<ViewDefinition>(viewName.db(),
                                                                   viewName.coll(),
                                                                   view["viewOn"].str(),
                                                                   pipeline,
                                                                   std::move(collator.getValue()));
        return Status::OK();
    };

    try {
        if (lookupBehavior == ViewCatalogLookupBehavior::kValidateDurableViews) {
            _durable->iterate(opCtx, reloadCallback);
        } else if (lookupBehavior == ViewCatalogLookupBehavior::kAllowInvalidDurableViews) {
            _durable->iterateIgnoreInvalidEntries(opCtx, reloadCallback);
        } else {
            MONGO_UNREACHABLE;
        }
    } catch (const DBException& ex) {
        auto status = ex.toStatus();
        LOG(0) << "could not load view catalog for database " << _durable->getName() << ": "
               << status;
        return status;
    }

    _valid.store(true);
    return Status::OK();
}

void ViewCatalog::iterate(OperationContext* opCtx, ViewIteratorCallback callback) {
    Lock::CollectionLock systemViewsLock(
        opCtx,
        NamespaceString(_durable->getName(), NamespaceString::kSystemDotViewsCollectionName),
        MODE_IS);
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    uassertStatusOK(_reloadIfNeeded(lk, opCtx, ViewCatalogLookupBehavior::kValidateDurableViews));
    for (auto&& view : _viewMap) {
        callback(*view.second);
    }
}

Status ViewCatalog::_createOrUpdateView(WithLock lk,
                                        OperationContext* opCtx,
                                        const NamespaceString& viewName,
                                        const NamespaceString& viewOn,
                                        const BSONArray& pipeline,
                                        std::unique_ptr<CollatorInterface> collator) {
    invariant(opCtx->lockState()->isDbLockedForMode(viewName.db(), MODE_IX));
    invariant(opCtx->lockState()->isCollectionLockedForMode(viewName, MODE_IX));
    invariant(opCtx->lockState()->isCollectionLockedForMode(
        NamespaceString(viewName.db(), NamespaceString::kSystemDotViewsCollectionName), MODE_X));

    _requireValidCatalog(lk, opCtx);

    // Build the BSON definition for this view to be saved in the durable view catalog. If the
    // collation is empty, omit it from the definition altogether.
    BSONObjBuilder viewDefBuilder;
    viewDefBuilder.append("_id", viewName.ns());
    viewDefBuilder.append("viewOn", viewOn.coll());
    viewDefBuilder.append("pipeline", pipeline);
    if (collator) {
        viewDefBuilder.append("collation", collator->getSpec().toBSON());
    }

    BSONObj ownedPipeline = pipeline.getOwned();
    auto view = std::make_shared<ViewDefinition>(
        viewName.db(), viewName.coll(), viewOn.coll(), ownedPipeline, std::move(collator));

    // Check that the resulting dependency graph is acyclic and within the maximum depth.
    Status graphStatus = _upsertIntoGraph(lk, opCtx, *(view.get()));
    if (!graphStatus.isOK()) {
        return graphStatus;
    }

    _durable->upsert(opCtx, viewName, viewDefBuilder.obj());
    _viewMap[viewName.ns()] = view;
    opCtx->recoveryUnit()->onRollback([this, viewName]() {
        this->_viewMap.erase(viewName.ns());
        this->_viewGraphNeedsRefresh = true;
    });

    // We may get invalidated, but we're exclusively locked, so the change must be ours.
    opCtx->recoveryUnit()->onCommit(
        [this](boost::optional<Timestamp>) { this->_valid.store(true); });
    return Status::OK();
}

Status ViewCatalog::_upsertIntoGraph(WithLock lk,
                                     OperationContext* opCtx,
                                     const ViewDefinition& viewDef) {

    // Performs the insert into the graph.
    auto doInsert = [this, &lk, &opCtx](const ViewDefinition& viewDef,
                                        bool needsValidation) -> Status {
        // Validate that the pipeline is eligible to serve as a view definition. If it is, this
        // will also return the set of involved namespaces.
        auto pipelineStatus = _validatePipeline(lk, opCtx, viewDef);
        if (!pipelineStatus.isOK()) {
            if (needsValidation) {
                uassertStatusOKWithContext(pipelineStatus.getStatus(),
                                           str::stream() << "Invalid pipeline for view "
                                                         << viewDef.name().ns());
            }
            return pipelineStatus.getStatus();
        }

        auto involvedNamespaces = pipelineStatus.getValue();
        std::vector<NamespaceString> refs(involvedNamespaces.begin(), involvedNamespaces.end());
        refs.push_back(viewDef.viewOn());

        int pipelineSize = 0;
        for (auto obj : viewDef.pipeline()) {
            pipelineSize += obj.objsize();
        }

        if (needsValidation) {
            // Check the collation of all the dependent namespaces before updating the graph.
            auto collationStatus = _validateCollation(lk, opCtx, viewDef, refs);
            if (!collationStatus.isOK()) {
                return collationStatus;
            }
            return _viewGraph.insertAndValidate(viewDef, refs, pipelineSize);
        } else {
            _viewGraph.insertWithoutValidating(viewDef, refs, pipelineSize);
            return Status::OK();
        }
    };

    if (_viewGraphNeedsRefresh) {
        _viewGraph.clear();
        for (auto&& iter : _viewMap) {
            auto status = doInsert(*(iter.second.get()), false);
            // If we cannot fully refresh the graph, we will keep '_viewGraphNeedsRefresh' true.
            if (!status.isOK()) {
                return status;
            }
        }
        // Only if the inserts completed without error will we no longer need a refresh.
        opCtx->recoveryUnit()->onRollback([this]() { this->_viewGraphNeedsRefresh = true; });
        _viewGraphNeedsRefresh = false;
    }

    // Remove the view definition first in case this is an update. If it is not in the graph, it
    // is simply a no-op.
    _viewGraph.remove(viewDef.name());

    return doInsert(viewDef, true);
}

StatusWith<stdx::unordered_set<NamespaceString>> ViewCatalog::_validatePipeline(
    WithLock lk, OperationContext* opCtx, const ViewDefinition& viewDef) const {
    AggregationRequest request(viewDef.viewOn(), viewDef.pipeline());
    const LiteParsedPipeline liteParsedPipeline(request);
    const auto involvedNamespaces = liteParsedPipeline.getInvolvedNamespaces();

    // Verify that this is a legitimate pipeline specification by making sure it parses
    // correctly. In order to parse a pipeline we need to resolve any namespaces involved to a
    // collection and a pipeline, but in this case we don't need this map to be accurate since
    // we will not be evaluating the pipeline.
    StringMap<ExpressionContext::ResolvedNamespace> resolvedNamespaces;
    for (auto&& nss : involvedNamespaces) {
        resolvedNamespaces[nss.coll()] = {nss, {}};
    }
    boost::intrusive_ptr<ExpressionContext> expCtx =
        new ExpressionContext(opCtx,
                              request,
                              CollatorInterface::cloneCollator(viewDef.defaultCollator()),
                              // We can use a stub MongerProcessInterface because we are only parsing
                              // the Pipeline for validation here. We won't do anything with the
                              // pipeline that will require a real implementation.
                              std::make_shared<StubMongerProcessInterface>(),
                              std::move(resolvedNamespaces),
                              boost::none);

    // Save this to a variable to avoid reading the atomic variable multiple times.
    auto currentFCV = serverGlobalParams.featureCompatibility.getVersion();

    // If the feature compatibility version is not 4.2, and we are validating features as master,
    // ban the use of new agg features introduced in 4.2 to prevent them from being persisted in the
    // catalog.
    if (serverGlobalParams.validateFeaturesAsMaster.load() &&
        currentFCV != ServerGlobalParams::FeatureCompatibility::Version::kFullyUpgradedTo42) {
        expCtx->maxFeatureCompatibilityVersion = currentFCV;
    }
    auto pipelineStatus = Pipeline::parse(viewDef.pipeline(), std::move(expCtx));
    if (!pipelineStatus.isOK()) {
        return pipelineStatus.getStatus();
    }

    // Validate that the view pipeline does not contain any ineligible stages.
    const auto& sources = pipelineStatus.getValue()->getSources();
    if (!sources.empty()) {
        const auto firstPersistentStage =
            std::find_if(sources.begin(), sources.end(), [](const auto& source) {
                return source->constraints().writesPersistentData();
            });
        if (sources.front()->constraints().isChangeStreamStage()) {
            return {ErrorCodes::OptionNotSupportedOnView,
                    "$changeStream cannot be used in a view definition"};
        } else if (firstPersistentStage != sources.end()) {
            monger::StringBuilder errorMessage;
            errorMessage << "The aggregation stage " << firstPersistentStage->get()->getSourceName()
                         << " in location " << std::distance(sources.begin(), firstPersistentStage)
                         << " of the pipeline cannot be used in the view definition of "
                         << viewDef.name().ns() << " because it writes to disk";
            return {ErrorCodes::OptionNotSupportedOnView, errorMessage.str()};
        }
    }

    return std::move(involvedNamespaces);
}

Status ViewCatalog::_validateCollation(WithLock lk,
                                       OperationContext* opCtx,
                                       const ViewDefinition& view,
                                       const std::vector<NamespaceString>& refs) {
    for (auto&& potentialViewNss : refs) {
        auto otherView = _lookup(
            lk, opCtx, potentialViewNss.ns(), ViewCatalogLookupBehavior::kValidateDurableViews);
        if (otherView &&
            !CollatorInterface::collatorsMatch(view.defaultCollator(),
                                               otherView->defaultCollator())) {
            return {ErrorCodes::OptionNotSupportedOnView,
                    str::stream() << "View " << view.name().toString()
                                  << " has conflicting collation with view "
                                  << otherView->name().toString()};
        }
    }
    return Status::OK();
}

Status ViewCatalog::createView(OperationContext* opCtx,
                               const NamespaceString& viewName,
                               const NamespaceString& viewOn,
                               const BSONArray& pipeline,
                               const BSONObj& collation) {

    invariant(opCtx->lockState()->isDbLockedForMode(viewName.db(), MODE_IX));
    invariant(opCtx->lockState()->isCollectionLockedForMode(viewName, MODE_IX));
    invariant(opCtx->lockState()->isCollectionLockedForMode(
        NamespaceString(viewName.db(), NamespaceString::kSystemDotViewsCollectionName), MODE_X));

    stdx::lock_guard<stdx::mutex> lk(_mutex);

    if (viewName.db() != viewOn.db())
        return Status(ErrorCodes::BadValue,
                      "View must be created on a view or collection in the same database");

    if (_lookup(
            lk, opCtx, StringData(viewName.ns()), ViewCatalogLookupBehavior::kValidateDurableViews))
        return Status(ErrorCodes::NamespaceExists, "Namespace already exists");

    if (!NamespaceString::validCollectionName(viewOn.coll()))
        return Status(ErrorCodes::InvalidNamespace,
                      str::stream() << "invalid name for 'viewOn': " << viewOn.coll());

    if (viewName.isSystem())
        return Status(
            ErrorCodes::InvalidNamespace,
            "View name cannot start with 'system.', which is reserved for system namespaces");

    auto collator = parseCollator(opCtx, collation);
    if (!collator.isOK())
        return collator.getStatus();

    return _createOrUpdateView(
        lk, opCtx, viewName, viewOn, pipeline, std::move(collator.getValue()));
}

Status ViewCatalog::modifyView(OperationContext* opCtx,
                               const NamespaceString& viewName,
                               const NamespaceString& viewOn,
                               const BSONArray& pipeline) {
    invariant(opCtx->lockState()->isDbLockedForMode(viewName.db(), MODE_X));

    stdx::lock_guard<stdx::mutex> lk(_mutex);

    if (viewName.db() != viewOn.db())
        return Status(ErrorCodes::BadValue,
                      "View must be created on a view or collection in the same database");

    auto viewPtr =
        _lookup(lk, opCtx, viewName.ns(), ViewCatalogLookupBehavior::kValidateDurableViews);
    if (!viewPtr)
        return Status(ErrorCodes::NamespaceNotFound,
                      str::stream() << "cannot modify missing view " << viewName.ns());

    if (!NamespaceString::validCollectionName(viewOn.coll()))
        return Status(ErrorCodes::InvalidNamespace,
                      str::stream() << "invalid name for 'viewOn': " << viewOn.coll());

    ViewDefinition savedDefinition = *viewPtr;
    opCtx->recoveryUnit()->onRollback([this, viewName, savedDefinition]() {
        this->_viewMap[viewName.ns()] = std::make_shared<ViewDefinition>(savedDefinition);
    });

    return _createOrUpdateView(lk,
                               opCtx,
                               viewName,
                               viewOn,
                               pipeline,
                               CollatorInterface::cloneCollator(savedDefinition.defaultCollator()));
}

Status ViewCatalog::dropView(OperationContext* opCtx, const NamespaceString& viewName) {
    invariant(opCtx->lockState()->isDbLockedForMode(viewName.db(), MODE_IX));
    invariant(opCtx->lockState()->isCollectionLockedForMode(viewName, MODE_IX));
    invariant(opCtx->lockState()->isCollectionLockedForMode(
        NamespaceString(viewName.db(), NamespaceString::kSystemDotViewsCollectionName), MODE_X));

    stdx::lock_guard<stdx::mutex> lk(_mutex);
    _requireValidCatalog(lk, opCtx);

    // Save a copy of the view definition in case we need to roll back.
    auto viewPtr =
        _lookup(lk, opCtx, viewName.ns(), ViewCatalogLookupBehavior::kValidateDurableViews);
    if (!viewPtr) {
        return {ErrorCodes::NamespaceNotFound,
                str::stream() << "cannot drop missing view: " << viewName.ns()};
    }

    ViewDefinition savedDefinition = *viewPtr;

    invariant(_valid.load());
    _durable->remove(opCtx, viewName);
    _viewGraph.remove(savedDefinition.name());
    _viewMap.erase(viewName.ns());
    opCtx->recoveryUnit()->onRollback([this, viewName, savedDefinition]() {
        this->_viewGraphNeedsRefresh = true;
        this->_viewMap[viewName.ns()] = std::make_shared<ViewDefinition>(savedDefinition);
    });

    // We may get invalidated, but we're exclusively locked, so the change must be ours.
    opCtx->recoveryUnit()->onCommit(
        [this](boost::optional<Timestamp>) { this->_valid.store(true); });
    return Status::OK();
}

std::shared_ptr<ViewDefinition> ViewCatalog::_lookup(WithLock lk,
                                                     OperationContext* opCtx,
                                                     StringData ns,
                                                     ViewCatalogLookupBehavior lookupBehavior) {
    // We expect the catalog to be valid, so short-circuit other checks for best performance.
    if (MONGO_unlikely(!_valid.load())) {
        // If the catalog is invalid, we want to avoid references to virtualized or other invalid
        // collection names to trigger a reload. This makes the system more robust in presence of
        // invalid view definitions.
        if (!NamespaceString::validCollectionName(ns))
            return nullptr;
        Status status = _reloadIfNeeded(lk, opCtx, lookupBehavior);
        // In case of errors we've already logged a message. Only uassert if there actually is
        // a user connection, as otherwise we'd crash the server. The catalog will remain invalid,
        // and any views after the first invalid one are ignored.
        if (opCtx->getClient()->isFromUserConnection())
            uassertStatusOK(status);
    }

    ViewMap::const_iterator it = _viewMap.find(ns);
    if (it != _viewMap.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<ViewDefinition> ViewCatalog::lookup(OperationContext* opCtx, StringData ns) {
    Lock::CollectionLock systemViewsLock(
        opCtx,
        NamespaceString(_durable->getName(), NamespaceString::kSystemDotViewsCollectionName),
        MODE_IS);
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    return _lookup(lk, opCtx, ns, ViewCatalogLookupBehavior::kValidateDurableViews);
}

std::shared_ptr<ViewDefinition> ViewCatalog::lookupWithoutValidatingDurableViews(
    OperationContext* opCtx, StringData ns) {
    Lock::CollectionLock systemViewsLock(
        opCtx,
        NamespaceString(_durable->getName(), NamespaceString::kSystemDotViewsCollectionName),
        MODE_IS);
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    return _lookup(lk, opCtx, ns, ViewCatalogLookupBehavior::kAllowInvalidDurableViews);
}

StatusWith<ResolvedView> ViewCatalog::resolveView(OperationContext* opCtx,
                                                  const NamespaceString& nss) {
    Lock::CollectionLock systemViewsLock(
        opCtx,
        NamespaceString(_durable->getName(), NamespaceString::kSystemDotViewsCollectionName),
        MODE_IS);
    stdx::unique_lock<stdx::mutex> lock(_mutex);

    // Keep looping until the resolution completes. If the catalog is invalidated during the
    // resolution, we start over from the beginning.
    while (true) {
        // Points to the name of the most resolved namespace.
        const NamespaceString* resolvedNss = &nss;

        // Holds the combination of all the resolved views.
        std::vector<BSONObj> resolvedPipeline;

        // If the catalog has not been tampered with, all views seen during the resolution will have
        // the same collation. As an optimization, we fill out the collation spec only once.
        boost::optional<BSONObj> collation;

        // The last seen view definition, which owns the NamespaceString pointed to by
        // 'resolvedNss'.
        std::shared_ptr<ViewDefinition> lastViewDefinition;

        int depth = 0;
        for (; depth < ViewGraph::kMaxViewDepth; depth++) {
            // If the catalog has been invalidated, bail and restart.
            if (!_valid.load()) {
                uassertStatusOK(
                    _reloadIfNeeded(lock, opCtx, ViewCatalogLookupBehavior::kValidateDurableViews));
                break;
            }

            auto view = _lookup(
                lock, opCtx, resolvedNss->ns(), ViewCatalogLookupBehavior::kValidateDurableViews);
            if (!view) {
                // Return error status if pipeline is too large.
                int pipelineSize = 0;
                for (auto obj : resolvedPipeline) {
                    pipelineSize += obj.objsize();
                }
                if (pipelineSize > ViewGraph::kMaxViewPipelineSizeBytes) {
                    return {ErrorCodes::ViewPipelineMaxSizeExceeded,
                            str::stream() << "View pipeline exceeds maximum size; maximum size is "
                                          << ViewGraph::kMaxViewPipelineSizeBytes};
                }
                return StatusWith<ResolvedView>(
                    {*resolvedNss,
                     std::move(resolvedPipeline),
                     collation ? std::move(collation.get()) : CollationSpec::kSimpleSpec});
            }

            resolvedNss = &view->viewOn();
            if (!collation) {
                collation = view->defaultCollator() ? view->defaultCollator()->getSpec().toBSON()
                                                    : CollationSpec::kSimpleSpec;
            }

            // Prepend the underlying view's pipeline to the current working pipeline.
            const std::vector<BSONObj>& toPrepend = view->pipeline();
            resolvedPipeline.insert(resolvedPipeline.begin(), toPrepend.begin(), toPrepend.end());

            // If the first stage is a $collStats, then we return early with the viewOn namespace.
            if (toPrepend.size() > 0 && !toPrepend[0]["$collStats"].eoo()) {
                return StatusWith<ResolvedView>(
                    {*resolvedNss, std::move(resolvedPipeline), std::move(collation.get())});
            }
        }

        if (depth >= ViewGraph::kMaxViewDepth) {
            return {ErrorCodes::ViewDepthLimitExceeded,
                    str::stream() << "View depth too deep or view cycle detected; maximum depth is "
                                  << ViewGraph::kMaxViewDepth};
        }
    };
    MONGO_UNREACHABLE;
}
}  // namespace monger
