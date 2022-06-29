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

#include "extensions/shell/neva/neva_url_loader_throttle.h"

#include "extensions/shell/neva/malware_detection_service.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/resource_request.h"
#include "url/gurl.h"
#include "url/url_util.h"

namespace neva {

NevaURLLoaderThrottle::NevaURLLoaderThrottle() = default;

NevaURLLoaderThrottle::~NevaURLLoaderThrottle() = default;

NevaURLLoaderThrottle::NevaURLLoaderThrottle(
    MalwareDetectionService* malware_detection_service)
    : malware_detection_service_(malware_detection_service) {}

void NevaURLLoaderThrottle::CheckURL(const GURL& url) {
  if (!malware_detection_service_) {
    delegate_->Resume();
    return;
  }

  if (IsValidURL(url)) {
    pending_checks_++;
    malware_detection_service_->CheckURL(
        url, base::BindOnce(&NevaURLLoaderThrottle::OnCheckComplete,
                            weak_factory_.GetWeakPtr()));
  } else {
    VLOG(1) << __func__ << " Failed, Invalid URL !!";
  }
}

void NevaURLLoaderThrottle::OnCheckComplete(bool is_safe) {
  pending_checks_--;

  if (is_safe) {
    if (pending_checks_ == 0 && deferred_request_) {
      deferred_request_ = false;
      delegate_->Resume();
      VLOG(2) << __func__ << " Navigation resumed for Safe URL";
    }
  } else {
    VLOG(2) << __func__ << " Malware Site is blocked!!";

    blocked_ = true;
    pending_checks_ = 0;
    delegate_->CancelWithError(net::ERR_BLOCKED_BY_MALWARE_SITES,
                               "MalwareSite");
  }
}

void NevaURLLoaderThrottle::WillStartRequest(network::ResourceRequest* request,
                                             bool* defer) {
  CheckURL(request->url);
}

void NevaURLLoaderThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& response_head,
    bool* defer,
    std::vector<std::string>* to_be_removed_headers,
    net::HttpRequestHeaders* modified_headers,
    net::HttpRequestHeaders* modified_cors_exempt_headers) {
  if (blocked_) {
    *defer = true;
    return;
  }

  CheckURL(redirect_info->new_url);
}

void NevaURLLoaderThrottle::WillProcessResponse(
    const GURL& response_url,
    network::mojom::URLResponseHead* response_head,
    bool* defer) {
  if (blocked_) {
    *defer = true;
    return;
  }

  if (pending_checks_ == 0)
    return;

  deferred_request_ = true;
  *defer = true;
}

bool NevaURLLoaderThrottle::IsValidURLScheme(const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() || url.SchemeIs(url::kFtpScheme) ||
      url.SchemeIsWSOrWSS();
}

bool NevaURLLoaderThrottle::IsValidURL(const GURL& url) {
  if (url.spec().empty()) {
    VLOG(2) << __func__ << ", url spec is not valid EMPTY";
    return false;
  }

  if (!IsValidURLScheme(url)) {
    VLOG(2) << __func__ << ", URL cannot be checked by scheme: " << url;
    return false;
  }

  return true;
}

}  // namespace neva