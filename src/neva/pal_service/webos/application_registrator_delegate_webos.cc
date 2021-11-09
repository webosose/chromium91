// Copyright 2019 LG Electronics, Inc.
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

#include "neva/pal_service/webos/application_registrator_delegate_webos.h"

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "neva/pal_service/luna/luna_names.h"

namespace {

const char kEvent[] = "event";
const char kParameters[] = "parameters";
const char kReason[] = "reason";
const char kRegisterNativeAppMethod[] = "registerNativeApp";
const char kRegisterAppRequest[] = R"JSON({"subscribe":true})JSON";

}  // namespace

namespace pal {
namespace webos {

ApplicationRegistratorDelegateWebOS::ApplicationRegistratorDelegateWebOS(
    const std::string& application_id,
    const std::string& application_name,
    RepeatingResponse callback)
    : application_id_(application_id),
      application_name_(application_name),
      callback_(std::move(callback)) {
  pal::luna::Client::Params params;
  params.name = application_name_;
  luna_client_ = pal::luna::GetSharedClient(params);

  if (luna_client_ && luna_client_->IsInitialized()) {
    const bool subscribed = luna_client_->SubscribeFromApp(
        luna::GetServiceURI(luna::service_uri::kApplicationManager,
                            kRegisterNativeAppMethod),
        std::string(kRegisterAppRequest), application_id_,
        base::BindRepeating(&ApplicationRegistratorDelegateWebOS::OnResponse,
                            base::Unretained(this)));
    status_ = subscribed ? Status::kSuccess : Status::kFailed;
  }
}

ApplicationRegistratorDelegateWebOS::~ApplicationRegistratorDelegateWebOS() {}

ApplicationRegistratorDelegate::Status
ApplicationRegistratorDelegateWebOS::GetStatus() const {
  return status_;
}

std::string ApplicationRegistratorDelegateWebOS::GetApplicationName() const {
  return application_name_;
};

void ApplicationRegistratorDelegateWebOS::OnResponse(
    pal::luna::Client::ResponseStatus,
    unsigned,
    const std::string& json) {
  base::Optional<base::Value> root(base::JSONReader::Read(json));
  if (!root || !root->is_dict())
    return;

  base::Optional<bool> return_value = root->FindBoolKey("returnValue");

  if (return_value.has_value() && !return_value.value()) {
    const std::string* message = root->FindStringKey("errorText");
    LOG(ERROR) << __func__ << "(): Failed to register application. Error: "
               << (message ? *message : "");

    return;
  }

  const std::string* event = root->FindStringKey(kEvent);
  const std::string* reason = root->FindStringKey(kReason);
  const base::Value* parameters = root->FindDictKey(kParameters);
  if (event)
    callback_.Run(*event, *reason, parameters);
}

}  // namespace webos
}  // namespace pal
