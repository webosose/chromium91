// Copyright 2022 LG Electronics, Inc.
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

#include "extensions/shell/neva/webrisk/core/webrisk_fetch_hashes.h"

#include "base/base64.h"
#include "base/base64url.h"
#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/values.h"
#include "extensions/shell/neva/webrisk/core/webrisk.pb.h"
#include "extensions/shell/neva/webrisk/core/webrisk_store.h"
#include "net/base/escape.h"
#include "net/base/net_errors.h"
#include "net/http/http_status_code.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"
#include "url/url_util.h"

namespace webrisk {

namespace {

const char kCompressionTypeRAW[] = "RAW";
const char kApiKeyInvalidResp[] = "API_KEY_INVALID";

}  // namespace

WebRiskFetchHashes::WebRiskFetchHashes(
    const std::string& webrisk_key,
    scoped_refptr<WebRiskStore> webrisk_store,
    network::SharedURLLoaderFactory* url_loader_factory,
    FetchHashStatusCallback callback)
    : webrisk_key_(webrisk_key),
      webrisk_store_(std::move(webrisk_store)),
      url_loader_factory_(url_loader_factory),
      fetch_status_callback_(std::move(callback)) {}

WebRiskFetchHashes::~WebRiskFetchHashes() = default;

void WebRiskFetchHashes::ComputeDiffRequest() {
  VLOG(2) << __func__;

  const std::string kMethod = "GET";
  std::string api_endpoint_url =
      "https://webrisk.googleapis.com/v1/threatLists:computeDiff?";
  api_endpoint_url += "threatType=";
  api_endpoint_url += WebRiskStore::kThreatTypeMalware;
  api_endpoint_url += "&constraints.supportedCompressions=";
  api_endpoint_url += kCompressionTypeRAW;
  api_endpoint_url += "&key=";
  api_endpoint_url += webrisk_key_;

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(api_endpoint_url);
  request->method = kMethod;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  url_loader_ = network::SimpleURLLoader::Create(std::move(request),
                                                 MISSING_TRAFFIC_ANNOTATION);
  url_loader_->SetAllowHttpErrorResults(true);

  url_loader_->DownloadToString(
      url_loader_factory_,
      base::BindOnce(&WebRiskFetchHashes::OnComputeDiffResponse,
                     base::Unretained(this), api_endpoint_url),
      WebRiskStore::kMaxWebRiskStoreSize);
}

void WebRiskFetchHashes::OnComputeDiffResponse(
    const std::string& url,
    std::unique_ptr<std::string> response_body) {
  int response_code = -1;  // Invalid response code.
  std::string response_body_data = std::move(*response_body);
  if (url_loader_ && url_loader_->ResponseInfo() &&
      url_loader_->ResponseInfo()->headers) {
    response_code = url_loader_->ResponseInfo()->headers->response_code();
  } else {
    VLOG(1) << __func__ << ", Failed response !!";
    fetch_status_callback_.Run(kFailed);
    return;
  }

  VLOG(2) << __func__ << " URL = " << url
          << " ContentSize = " << url_loader_->GetContentSize()
          << " Response_code = " << response_code
          << " NetError = " << url_loader_->NetError();

  if (response_code != 200) {
    // API_KEY_INVALID
    if ((response_code == 400) &&
        (response_body_data.find(kApiKeyInvalidResp) != std::string::npos)) {
      VLOG(1) << __func__ << " Failed, Invalid API Key !!";
      fetch_status_callback_.Run(kInvalidKey);
      return;
    }

    VLOG(1) << __func__ << ", Failed response !!";
    fetch_status_callback_.Run(kFailed);
    return;
  }

  base::Optional<base::Value> response_dict =
      base::JSONReader::Read(response_body_data);
  if (!response_dict.has_value()) {
    VLOG(1) << __func__ << ", Failed to response body !!";
    fetch_status_callback_.Run(kFailed);
    return;
  }

  ComputeThreatListDiffResponse file_format;
  bool parse_status =
      ParseJSONToUpdateResponse(response_body_data, file_format);
  if (!parse_status) {
    VLOG(1) << __func__ << ", Failed to read response!!";
    fetch_status_callback_.Run(kFailed);
    return;
  }

  bool status = webrisk_store_->WriteToDisk(file_format);
  if (!status) {
    VLOG(1) << __func__ << ", Failed to write to store !!";
    fetch_status_callback_.Run(kFailed);
    return;
  }

  base::TimeDelta next_update_time = webrisk_store_->GetNextUpdateTime(
      file_format.recommended_next_diff());
  if (next_update_time > base::TimeDelta()) {
    VLOG(2) << __func__ << ", next_update_time= " << next_update_time;
    ScheduleComputeDiffRequestInternal(next_update_time);
  }

  fetch_status_callback_.Run(kSuccess);
  url_loader_.reset();
}

void WebRiskFetchHashes::ScheduleComputeDiffRequest(base::TimeDelta interval) {
  if (IsUpdateScheduled()) {
    VLOG(1) << __func__ << " Update is already scheduled";
    return;
  }

  ScheduleComputeDiffRequestInternal(interval);
}

void WebRiskFetchHashes::ScheduleComputeDiffRequestInternal(
    base::TimeDelta interval) {
  VLOG(2) << __func__ << ", Interval: " << interval;

  if (interval <= base::TimeDelta()) {
    ComputeDiffRequest();
  } else {
    // Database store is present. Return success.
    fetch_status_callback_.Run(kSuccess);

    update_timer_.Stop();
    update_timer_.Start(FROM_HERE, interval, this,
                        &WebRiskFetchHashes::ComputeDiffRequest);
  }
}

bool WebRiskFetchHashes::IsUpdateScheduled() const {
  return update_timer_.IsRunning();
}

bool WebRiskFetchHashes::ParseJSONToUpdateResponse(
    const std::string& response_body,
    ComputeThreatListDiffResponse& file_format) {
  base::Optional<base::Value> response_dict =
      base::JSONReader::Read(response_body);

  if (!response_dict.has_value())
    return false;

  const std::string* next_diff =
      response_dict->FindStringKey("recommendedNextDiff");
  if (next_diff)
    file_format.set_recommended_next_diff(*next_diff);

  const std::string* response_type =
      response_dict->FindStringKey("responseType");
  if (response_type && !response_type->compare("RESET"))
    file_format.set_response_type(ComputeThreatListDiffResponse::RESET);

  base::Value* addition_data = response_dict->FindDictKey("additions");
  if (addition_data) {
    // Null check is not required for "ThreatEntryAdditions*" and "Checksum*"
    // as proto implementation will always return valid instances
    ThreatEntryAdditions* additions = file_format.mutable_additions();
    base::Value* raw_hashes = addition_data->FindListKey("rawHashes");
    if (raw_hashes) {
      for (const base::Value& item : raw_hashes->GetList()) {
        auto prefix_size = item.FindIntKey("prefixSize");
        RawHashes* raw_hash_list = additions->add_raw_hashes();
        if (raw_hash_list && prefix_size) {
          raw_hash_list->set_prefix_size(*prefix_size);
          const std::string* hashlist_b64 = item.FindStringKey("rawHashes");
          if (hashlist_b64)
            raw_hash_list->set_raw_hashes(*hashlist_b64);
        }
      }
    }
  }

  const std::string* version_token =
      response_dict->FindStringKey("newVersionToken");
  if (version_token)
    file_format.set_new_version_token(*version_token);

  base::Value* checksum_256 = response_dict->FindDictKey("checksum");
  if (checksum_256) {
    const std::string* sha256 = checksum_256->FindStringKey("sha256");
    if (sha256) {
      ComputeThreatListDiffResponse::Checksum* checksum_sha256 =
          file_format.mutable_checksum();
      checksum_sha256->set_sha256(*sha256);
    }
  }

  return true;
}

}  // namespace webrisk
