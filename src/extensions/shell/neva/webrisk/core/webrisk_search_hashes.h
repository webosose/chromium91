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

#ifndef EXTENSIONS_SHELL_NEVA_WEBRISK_CORE_SEARCH_HASHES_API_H_
#define EXTENSIONS_SHELL_NEVA_WEBRISK_CORE_SEARCH_HASHES_API_H_

#include <set>

#include "base/callback.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}

namespace webrisk {

class WebRiskSearchHashes {
 public:
  typedef base::OnceCallback<void(bool status)> SearchHashesCallback;

  WebRiskSearchHashes(const std::string& webrisk_key,
                      network::SharedURLLoaderFactory* url_loader_factory);
  ~WebRiskSearchHashes();

  void SearchHashPrefix(const std::string& hash_prefix,
                        SearchHashesCallback callback);

 private:
  WebRiskSearchHashes() = delete;

  void OnSearchHashResponse(const std::string& url,
                            SearchHashesCallback callback,
                            std::unique_ptr<std::string> response_body);

  std::string webrisk_key_;
  std::unique_ptr<network::SimpleURLLoader> url_loader_;
  std::set<unsigned int> response_tokens_;
  network::SharedURLLoaderFactory* url_loader_factory_;
};

}  // namespace webrisk
#endif  // EXTENSIONS_SHELL_NEVA_WEBRISK_CORE_SEARCH_HASHES_API_H_
