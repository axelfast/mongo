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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kSharding

#include "monger/platform/basic.h"

#include <vector>

#include "monger/client/remote_command_targeter_mock.h"
#include "monger/db/commands.h"
#include "monger/db/query/query_request.h"
#include "monger/db/service_context.h"
#include "monger/executor/network_interface_mock.h"
#include "monger/executor/task_executor.h"
#include "monger/rpc/metadata/repl_set_metadata.h"
#include "monger/rpc/metadata/tracking_metadata.h"
#include "monger/s/catalog/config_server_version.h"
#include "monger/s/catalog/type_config_version.h"
#include "monger/s/client/shard_registry.h"
#include "monger/s/cluster_identity_loader.h"
#include "monger/s/sharding_router_test_fixture.h"
#include "monger/stdx/future.h"
#include "monger/util/log.h"
#include "monger/util/str.h"

namespace monger {
namespace {

using executor::NetworkInterfaceMock;
using executor::RemoteCommandRequest;
using executor::TaskExecutor;
using unittest::assertGet;

BSONObj getReplSecondaryOkMetadata() {
    BSONObjBuilder o;
    ReadPreferenceSetting(ReadPreference::Nearest).toContainingBSON(&o);
    o.append(rpc::kReplSetMetadataFieldName, 1);
    return o.obj();
}

class ClusterIdentityTest : public ShardingTestFixture {
public:
    ClusterIdentityTest() {
        configTargeter()->setFindHostReturnValue(configHost);
    }

    void expectConfigVersionLoad(StatusWith<OID> result) {
        onFindCommand([&](const RemoteCommandRequest& request) {
            ASSERT_EQUALS(configHost, request.target);
            ASSERT_BSONOBJ_EQ(getReplSecondaryOkMetadata(),
                              rpc::TrackingMetadata::removeTrackingData(request.metadata));

            const NamespaceString nss(request.dbname, request.cmdObj.firstElement().String());
            ASSERT_EQ(nss.ns(), "config.version");

            auto query = assertGet(QueryRequest::makeFromFindCommand(nss, request.cmdObj, false));

            ASSERT_EQ(query->nss().ns(), "config.version");
            ASSERT_BSONOBJ_EQ(query->getFilter(), BSONObj());
            ASSERT_FALSE(query->getLimit().is_initialized());

            if (result.isOK()) {
                VersionType version;
                version.setCurrentVersion(CURRENT_CONFIG_VERSION);
                version.setMinCompatibleVersion(MIN_COMPATIBLE_CONFIG_VERSION);
                version.setClusterId(result.getValue());

                return StatusWith<std::vector<BSONObj>>{{version.toBSON()}};
            } else {
                return StatusWith<std::vector<BSONObj>>{result.getStatus()};
            }
        });
    }

protected:
    OID clusterId{OID::gen()};
    HostAndPort configHost{"TestHost1"};
};

TEST_F(ClusterIdentityTest, BasicLoadSuccess) {

    // The first time you ask for the cluster ID it will have to be loaded from the config servers.
    auto future = launchAsync([&] {
        auto clusterIdStatus =
            ClusterIdentityLoader::get(operationContext())
                ->loadClusterId(operationContext(), repl::ReadConcernLevel::kMajorityReadConcern);
        ASSERT_OK(clusterIdStatus);
        ASSERT_EQUALS(clusterId, ClusterIdentityLoader::get(operationContext())->getClusterId());
    });

    expectConfigVersionLoad(clusterId);

    future.default_timed_get();

    // Subsequent requests for the cluster ID should not require any network traffic as we consult
    // the cached version.
    ASSERT_OK(
        ClusterIdentityLoader::get(operationContext())
            ->loadClusterId(operationContext(), repl::ReadConcernLevel::kMajorityReadConcern));
}

TEST_F(ClusterIdentityTest, MultipleThreadsLoadingSuccess) {
    // Check that multiple threads calling getClusterId at once still results in only one network
    // operation.
    auto future1 = launchAsync([&] {
        auto clusterIdStatus =
            ClusterIdentityLoader::get(operationContext())
                ->loadClusterId(operationContext(), repl::ReadConcernLevel::kMajorityReadConcern);
        ASSERT_OK(clusterIdStatus);
        ASSERT_EQUALS(clusterId, ClusterIdentityLoader::get(operationContext())->getClusterId());
    });
    auto future2 = launchAsync([&] {
        auto clusterIdStatus =
            ClusterIdentityLoader::get(operationContext())
                ->loadClusterId(operationContext(), repl::ReadConcernLevel::kMajorityReadConcern);
        ASSERT_OK(clusterIdStatus);
        ASSERT_EQUALS(clusterId, ClusterIdentityLoader::get(operationContext())->getClusterId());
    });
    auto future3 = launchAsync([&] {
        auto clusterIdStatus =
            ClusterIdentityLoader::get(operationContext())
                ->loadClusterId(operationContext(), repl::ReadConcernLevel::kMajorityReadConcern);
        ASSERT_OK(clusterIdStatus);
        ASSERT_EQUALS(clusterId, ClusterIdentityLoader::get(operationContext())->getClusterId());
    });

    expectConfigVersionLoad(clusterId);

    future1.default_timed_get();
    future2.default_timed_get();
    future3.default_timed_get();
}

TEST_F(ClusterIdentityTest, BasicLoadFailureFollowedBySuccess) {

    // The first time you ask for the cluster ID it will have to be loaded from the config servers.
    auto future = launchAsync([&] {
        auto clusterIdStatus =
            ClusterIdentityLoader::get(operationContext())
                ->loadClusterId(operationContext(), repl::ReadConcernLevel::kMajorityReadConcern);
        ASSERT_EQUALS(ErrorCodes::Interrupted, clusterIdStatus);
    });

    expectConfigVersionLoad(Status(ErrorCodes::Interrupted, "interrupted"));

    future.default_timed_get();

    // After a failure to load the cluster ID, subsequent attempts to get the cluster ID should
    // retry loading it.
    future = launchAsync([&] {
        auto clusterIdStatus =
            ClusterIdentityLoader::get(operationContext())
                ->loadClusterId(operationContext(), repl::ReadConcernLevel::kMajorityReadConcern);
        ASSERT_OK(clusterIdStatus);
        ASSERT_EQUALS(clusterId, ClusterIdentityLoader::get(operationContext())->getClusterId());
    });

    expectConfigVersionLoad(clusterId);

    future.default_timed_get();
}

}  // namespace
}  // namespace monger
