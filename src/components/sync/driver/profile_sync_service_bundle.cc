// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/driver/profile_sync_service_bundle.h"

#include <string>
#include <utility>

#include "base/callback_helpers.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/sync/base/sync_prefs.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_network_connection_tracker.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace syncer {

using testing::Return;

ProfileSyncServiceBundle::ProfileSyncServiceBundle()
    : identity_test_env_(&test_url_loader_factory_) {
  SyncPrefs::RegisterProfilePrefs(pref_service_.registry());
  identity_test_env_.SetAutomaticIssueOfAccessTokens(true);
}

ProfileSyncServiceBundle::~ProfileSyncServiceBundle() {}

std::unique_ptr<SyncClientMock>
ProfileSyncServiceBundle::CreateSyncClientMock() {
  auto sync_client = std::make_unique<testing::NiceMock<SyncClientMock>>();
  ON_CALL(*sync_client, GetPrefService()).WillByDefault(Return(&pref_service_));
  ON_CALL(*sync_client, GetSyncApiComponentFactory())
      .WillByDefault(Return(&component_factory_));
  ON_CALL(*sync_client, GetSyncInvalidationsService())
      .WillByDefault(Return(sync_invalidations_service()));
  return std::move(sync_client);
}

ProfileSyncService::InitParams ProfileSyncServiceBundle::CreateBasicInitParams(
    ProfileSyncService::StartBehavior start_behavior,
    std::unique_ptr<SyncClient> sync_client) {
  ProfileSyncService::InitParams init_params;

  init_params.start_behavior = start_behavior;
  init_params.sync_client = std::move(sync_client);
  init_params.identity_manager = identity_manager();
  init_params.network_time_update_callback = base::DoNothing();
  init_params.url_loader_factory =
      base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
          &test_url_loader_factory_);
  init_params.network_connection_tracker =
      network::TestNetworkConnectionTracker::GetInstance();
  init_params.debug_identifier = "dummyDebugName";

  return init_params;
}

}  // namespace syncer
