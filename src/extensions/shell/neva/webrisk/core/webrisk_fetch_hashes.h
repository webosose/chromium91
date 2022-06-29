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

#ifndef EXTENSIONS_SHELL_NEVA_WEBRISK_CORE_WEBRISK_FETCH_HASHES_H_
#define EXTENSIONS_SHELL_NEVA_WEBRISK_CORE_WEBRISK_FETCH_HASHES_H_

#include "base/memory/scoped_refptr.h"
#include "base/timer/timer.h"
#include "extensions/shell/neva/webrisk/core/webrisk.pb.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}

namespace webrisk {

class WebRiskStore;

class WebRiskFetchHashes {
 public:
  enum Status {
    kFailed,
    kInvalidKey,
    kSuccess,
  };

  typedef base::Callback<void(Status status)> FetchHashStatusCallback;

  WebRiskFetchHashes(const std::string& webrisk_key,
                     scoped_refptr<WebRiskStore> webrisk_store,
                     network::SharedURLLoaderFactory* url_loader_factory,
                     FetchHashStatusCallback callback);
  ~WebRiskFetchHashes();

  void ScheduleComputeDiffRequest(base::TimeDelta update_interval_diff);

 private:
  WebRiskFetchHashes() = delete;

  void ComputeDiffRequest();
  void OnComputeDiffResponse(const std::string& url,
                             std::unique_ptr<std::string> response_body);
  void ScheduleComputeDiffRequestInternal(base::TimeDelta update_interval_diff);
  bool IsUpdateScheduled() const;
  bool ParseJSONToUpdateResponse(const std::string& response_body,
                                 ComputeThreatListDiffResponse& file_format);

  std::unique_ptr<network::SimpleURLLoader> url_loader_;

  base::OneShotTimer update_timer_;

  FetchHashStatusCallback fetch_status_callback_;

  std::string webrisk_key_;
  scoped_refptr<WebRiskStore> webrisk_store_;
  network::SharedURLLoaderFactory* url_loader_factory_ = nullptr;
};

}  // namespace webrisk

#endif  // EXTENSIONS_SHELL_NEVA_WEBRISK_CORE_WEBRISK_FETCH_HASHES_H_
