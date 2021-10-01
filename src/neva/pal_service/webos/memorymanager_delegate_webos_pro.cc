// Copyright 2021 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "neva/pal_service/webos/memorymanager_delegate_webos_pro.h"

#include "base/json/json_reader.h"
#include "neva/pal_service/luna/luna_names.h"

namespace pal {
namespace webos {

namespace {
const char kSubscribeToThresholdChanged[] =
    R"JSON({"category":"/com/webos/memory", "method":"thresholdChanged"})JSON";
const char kGetMemoryStatusMethod[] = "getCurrentMemState";
}  // namespace

std::unique_ptr<MemoryManagerDelegateWebOS>
MemoryManagerDelegateWebOS::Create() {
  return std::make_unique<MemoryManagerDelegateWebOSPro>();
}

MemoryManagerDelegateWebOSPro::MemoryManagerDelegateWebOSPro()
    : weak_factory_(this) {
  weak_ptr_this_ = weak_factory_.GetWeakPtr();
}

MemoryManagerDelegateWebOSPro::~MemoryManagerDelegateWebOSPro() = default;

void MemoryManagerDelegateWebOSPro::GetMemoryStatus(OnceResponse callback) {
  luna_client_->Call(
      luna::GetServiceURI(luna::service_uri::kMemoryManager,
                          kGetMemoryStatusMethod),
      std::string(kGetMemoryStatusRequest),
      base::BindOnce(&MemoryManagerDelegateWebOSPro::OnMemoryStatus,
                     weak_ptr_this_, std::move(callback)),
      std::string(kDefaultResponse));
}

void MemoryManagerDelegateWebOSPro::SubscribeToLevelChanged(
    RepeatingResponse callback) {
  subscription_callback_ = std::move(callback);
  if (!subscribed_ && luna_client_->IsInitialized()) {
    subscription_token_ = luna_client_->Subscribe(
        luna::GetServiceURI(luna::service_uri::kPalmBus, kSignalAddMatch),
        std::string(kSubscribeToThresholdChanged),
        base::BindRepeating(&MemoryManagerDelegateWebOSPro::OnLevelChanged,
                            weak_ptr_this_));
    subscribed_ = true;
  }
}

void MemoryManagerDelegateWebOSPro::OnMemoryStatus(OnceResponse callback,
                                                   luna::Client::ResponseStatus,
                                                   unsigned,
                                                   const std::string& json) {
  auto root(base::JSONReader::Read(json));
  if (root.has_value()) {
    // webOS Pro com.webos.memorymanager
    const std::string* status = root->FindStringPath("currentLevel");

    if (status && !status->empty()) {
      const std::string response = ConvertToMemoryLevel(*status);
      LOG(INFO) << __func__ << " called, response: " << response;
      std::move(callback).Run(std::move(response));
    }
  }
}

void MemoryManagerDelegateWebOSPro::OnLevelChanged(luna::Client::ResponseStatus,
                                                   unsigned,
                                                   const std::string& json) {
  // json input example:
  // {"previous":"medium","current":"normal","remainCount":2,
  //  "foregroundAppId":"com.webos.app.enactbrowser"}
  if (!subscribed_)
    return;

  auto root(base::JSONReader::Read(json));

  if (!root.has_value())
    return;

  if (root->FindPath("current") && root->FindPath("previous")) {
    LOG(INFO) << __func__ << " called with json: " << json;
    const std::string* current = root->FindStringPath("current");
    LOG(INFO) << __func__ << " calling callback with current: " << current;
    subscription_callback_.Run(std::move(*current));
  } else {
    LOG(ERROR) << __func__ << " wrong json value: " << json;
  }
}

}  // namespace webos
}  // namespace pal
