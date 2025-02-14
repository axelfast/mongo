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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kStorage

#if defined(__linux__)
#include <sys/vfs.h>
#endif

#include "monger/platform/basic.h"

#include "monger/base/init.h"
#include "monger/db/catalog/collection_options.h"
#include "monger/db/jsobj.h"
#include "monger/db/service_context.h"
#include "monger/db/storage/storage_engine_impl.h"
#include "monger/db/storage/storage_engine_init.h"
#include "monger/db/storage/storage_engine_lock_file.h"
#include "monger/db/storage/storage_engine_metadata.h"
#include "monger/db/storage/storage_options.h"
#include "monger/db/storage/wiredtiger/wiredtiger_global_options.h"
#include "monger/db/storage/wiredtiger/wiredtiger_index.h"
#include "monger/db/storage/wiredtiger/wiredtiger_kv_engine.h"
#include "monger/db/storage/wiredtiger/wiredtiger_parameters_gen.h"
#include "monger/db/storage/wiredtiger/wiredtiger_record_store.h"
#include "monger/db/storage/wiredtiger/wiredtiger_server_status.h"
#include "monger/db/storage/wiredtiger/wiredtiger_util.h"
#include "monger/util/log.h"
#include "monger/util/processinfo.h"

namespace monger {

namespace {
class WiredTigerFactory : public StorageEngine::Factory {
public:
    virtual ~WiredTigerFactory() {}
    virtual StorageEngine* create(const StorageGlobalParams& params,
                                  const StorageEngineLockFile* lockFile) const {
        if (lockFile && lockFile->createdByUncleanShutdown()) {
            warning() << "Recovering data from the last clean checkpoint.";
        }

#if defined(__linux__)
// This is from <linux/magic.h> but that isn't available on all systems.
// Note that the magic number for ext4 is the same as ext2 and ext3.
#define EXT4_SUPER_MAGIC 0xEF53
        {
            struct statfs fs_stats;
            int ret = statfs(params.dbpath.c_str(), &fs_stats);

            if (ret == 0 && fs_stats.f_type == EXT4_SUPER_MAGIC) {
                log() << startupWarningsLog;
                log() << "** WARNING: Using the XFS filesystem is strongly recommended with the "
                         "WiredTiger storage engine"
                      << startupWarningsLog;
                log() << "**          See "
                         "http://dochub.mongerdb.org/core/prodnotes-filesystem"
                      << startupWarningsLog;
            }
        }
#endif

        size_t cacheMB = WiredTigerUtil::getCacheSizeMB(wiredTigerGlobalOptions.cacheSizeGB);
        const double memoryThresholdPercentage = 0.8;
        ProcessInfo p;
        if (p.supported()) {
            if (cacheMB > memoryThresholdPercentage * p.getMemSizeMB()) {
                log() << startupWarningsLog;
                log() << "** WARNING: The configured WiredTiger cache size is more than "
                      << memoryThresholdPercentage * 100 << "% of available RAM."
                      << startupWarningsLog;
                log() << "**          See "
                         "http://dochub.mongerdb.org/core/faq-memory-diagnostics-wt"
                      << startupWarningsLog;
            }
        }
        const bool ephemeral = false;
        const auto maxCacheOverflowMB =
            static_cast<size_t>(1024 * wiredTigerGlobalOptions.maxCacheOverflowFileSizeGB);
        WiredTigerKVEngine* kv =
            new WiredTigerKVEngine(getCanonicalName().toString(),
                                   params.dbpath,
                                   getGlobalServiceContext()->getFastClockSource(),
                                   wiredTigerGlobalOptions.engineConfig,
                                   cacheMB,
                                   maxCacheOverflowMB,
                                   params.dur,
                                   ephemeral,
                                   params.repair,
                                   params.readOnly);
        kv->setRecordStoreExtraOptions(wiredTigerGlobalOptions.collectionConfig);
        kv->setSortedDataInterfaceExtraOptions(wiredTigerGlobalOptions.indexConfig);
        // Intentionally leaked.
        new WiredTigerServerStatusSection(kv);
        auto* param = new WiredTigerEngineRuntimeConfigParameter("wiredTigerEngineRuntimeConfig",
                                                                 ServerParameterType::kRuntimeOnly);
        param->_data.second = kv;

        auto* maxCacheOverflowParam = new WiredTigerMaxCacheOverflowSizeGBParameter(
            "wiredTigerMaxCacheOverflowSizeGB", ServerParameterType::kRuntimeOnly);
        maxCacheOverflowParam->_data = {wiredTigerGlobalOptions.maxCacheOverflowFileSizeGB, kv};

        StorageEngineOptions options;
        options.directoryPerDB = params.directoryperdb;
        options.directoryForIndexes = wiredTigerGlobalOptions.directoryForIndexes;
        options.forRepair = params.repair;
        return new StorageEngineImpl(kv, options);
    }

    virtual StringData getCanonicalName() const {
        return kWiredTigerEngineName;
    }

    virtual Status validateCollectionStorageOptions(const BSONObj& options) const {
        return WiredTigerRecordStore::parseOptionsField(options).getStatus();
    }

    virtual Status validateIndexStorageOptions(const BSONObj& options) const {
        return WiredTigerIndex::parseIndexOptions(options).getStatus();
    }

    virtual Status validateMetadata(const StorageEngineMetadata& metadata,
                                    const StorageGlobalParams& params) const {
        Status status =
            metadata.validateStorageEngineOption("directoryPerDB", params.directoryperdb);
        if (!status.isOK()) {
            return status;
        }

        status = metadata.validateStorageEngineOption("directoryForIndexes",
                                                      wiredTigerGlobalOptions.directoryForIndexes);
        if (!status.isOK()) {
            return status;
        }

        // If the 'groupCollections' field does not exist in the 'storage.bson' file, the
        // data-format of existing tables is as if 'groupCollections' is false. Passing this in
        // prevents validation from accepting 'params.groupCollections' being true when a "group
        // collections" aware mongerd is launched on an 3.4- dbpath.
        const bool kDefaultGroupCollections = false;
        status =
            metadata.validateStorageEngineOption("groupCollections",
                                                 params.groupCollections,
                                                 boost::optional<bool>(kDefaultGroupCollections));
        if (!status.isOK()) {
            return status;
        }

        return Status::OK();
    }

    virtual BSONObj createMetadataOptions(const StorageGlobalParams& params) const {
        BSONObjBuilder builder;
        builder.appendBool("directoryPerDB", params.directoryperdb);
        builder.appendBool("directoryForIndexes", wiredTigerGlobalOptions.directoryForIndexes);
        builder.appendBool("groupCollections", params.groupCollections);
        return builder.obj();
    }

    bool supportsReadOnly() const final {
        return true;
    }
};

ServiceContext::ConstructorActionRegisterer registerWiredTiger(
    "WiredTigerEngineInit", [](ServiceContext* service) {
        registerStorageEngine(service, std::make_unique<WiredTigerFactory>());
    });
}  // namespace
}  // namespace monger
