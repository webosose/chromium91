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

#ifndef EXTENSIONS_SHELL_NEVA_NEVA_URL_LOAD_THROTTLE_H_
#define EXTENSIONS_SHELL_NEVA_NEVA_URL_LOAD_THROTTLE_H_

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"

namespace neva {

class MalwareDetectionService;

class NevaURLLoaderThrottle : public blink::URLLoaderThrottle {
 public:
  NevaURLLoaderThrottle();
  NevaURLLoaderThrottle(MalwareDetectionService* malware_detection_service);
  ~NevaURLLoaderThrottle();

  // URLLoaderThrottle implementation
  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;
  void WillRedirectRequest(
      net::RedirectInfo* redirect_info,
      const network::mojom::URLResponseHead& response_head,
      bool* defer,
      std::vector<std::string>* to_be_removed_headers,
      net::HttpRequestHeaders* modified_headers,
      net::HttpRequestHeaders* modified_cors_exempt_headers) override;
  void WillProcessResponse(const GURL& response_url,
                           network::mojom::URLResponseHead* response_head,
                           bool* defer) override;

 private:
  void CheckURL(const GURL& url);
  void OnCheckComplete(bool is_safe);
  bool IsValidURLScheme(const GURL& url);
  bool IsValidURL(const GURL& url);

  bool blocked_ = false;
  bool deferred_request_ = false;
  size_t pending_checks_ = 0;

  MalwareDetectionService* malware_detection_service_;
  base::WeakPtrFactory<NevaURLLoaderThrottle> weak_factory_{this};
};

}  // namespace neva

#endif  // EXTENSIONS_SHELL_NEVA_NEVA_URL_LOAD_THROTTLE_H_
