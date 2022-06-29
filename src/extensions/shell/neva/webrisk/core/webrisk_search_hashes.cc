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

#include "extensions/shell/neva/webrisk/core/webrisk_search_hashes.h"

#include "content/public/browser/storage_partition.h"
#include "extensions/shell/neva/webrisk/core/webrisk.pb.h"
#include "extensions/shell/neva/webrisk/core/webrisk_store.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace webrisk {

WebRiskSearchHashes::WebRiskSearchHashes(
    const std::string& webrisk_key,
    network::SharedURLLoaderFactory* url_loader_factory)
    : webrisk_key_(webrisk_key), url_loader_factory_(url_loader_factory) {}

WebRiskSearchHashes::~WebRiskSearchHashes() = default;

void WebRiskSearchHashes::SearchHashPrefix(const std::string& hash_prefix,
                                           SearchHashesCallback callback) {
  unsigned int token_id;
  constexpr int kRetryMode =
      network::SimpleURLLoader::RETRY_ON_NETWORK_CHANGE |
      network::SimpleURLLoader::RETRY_ON_NAME_NOT_RESOLVED;
  const int kMaxRetries = 3;

  const std::string kMethod = "GET";
  std::string api_endpoint_url =
      "https://webrisk.googleapis.com/v1/hashes:search";
  api_endpoint_url += (!webrisk_key_.empty() ? ("?key=" + webrisk_key_) : "?");

  std::string get_req_schema =
      "&threatTypes=" + std::string(WebRiskStore::kThreatTypeMalware);
  api_endpoint_url += get_req_schema;
  api_endpoint_url += "&hash_prefix=" + hash_prefix;

  VLOG(2) << __func__ << " api_endpoint_url= " << api_endpoint_url;

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(api_endpoint_url);
  request->method = kMethod;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  url_loader_ = network::SimpleURLLoader::Create(std::move(request),
                                                 MISSING_TRAFFIC_ANNOTATION);
  url_loader_->SetAllowHttpErrorResults(true);
  url_loader_->SetRetryOptions(kMaxRetries, kRetryMode);
  url_loader_->DownloadToString(
      url_loader_factory_,
      base::BindOnce(&WebRiskSearchHashes::OnSearchHashResponse,
                     base::Unretained(this), token_id, api_endpoint_url,
                     std::move(callback)),
      WebRiskStore::kMaxWebRiskStoreSize);
  response_tokens_.insert(token_id);
}

void WebRiskSearchHashes::OnSearchHashResponse(
    unsigned int token_id,
    const std::string& url,
    SearchHashesCallback callback,
    std::unique_ptr<std::string> response_body) {
  std::string response_body_data = std::move(*response_body);
  int response_code = -1;
  if (url_loader_ && url_loader_->ResponseInfo() &&
      url_loader_->ResponseInfo()->headers) {
    response_code = url_loader_->ResponseInfo()->headers->response_code();
  } else {
    return;
  }

  // Invalid Argument
  if (response_code == 400) {
    LOG(ERROR) << __func__ << " Failed, Invalid Argument !!";
    std::move(callback).Run(true);
    return;
  }

  bool is_safe = (url_loader_->NetError() == net::OK && response_code == 200) &&
                 (response_body_data.find("threatTypes") == std::string::npos);
  VLOG(2) << __func__ << " URL= " << url
          << ", is " << (is_safe ? "safe " : "malware ")
          << ", ContentSize = " << url_loader_->GetContentSize()
          << ", Response_code = " << response_code
          << ", NetError = " << url_loader_->NetError();

  response_tokens_.erase(token_id);
  url_loader_.reset();
  std::move(callback).Run(is_safe);
}

}  // namespace webrisk
