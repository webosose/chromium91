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

#ifndef EXTENSIONS_SHELL_NEVA_WEBRISK_CORE_WEBRISK_STORE_H_
#define EXTENSIONS_SHELL_NEVA_WEBRISK_CORE_WEBRISK_STORE_H_

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "extensions/shell/neva/webrisk/core/webrisk.pb.h"

namespace webrisk {

class WebRiskStore : public base::RefCounted<WebRiskStore> {
 public:
  // Threat type string to be sent with the request
  static constexpr char kThreatTypeMalware[] = "MALWARE";

  // Size of hash prefix to be used
  static constexpr size_t kHashPrefixSize = 4;

  // The maximum size of webrisk store file size
  static constexpr size_t kMaxWebRiskStoreSize = 1 * 1024 * 1024;  // 1MB

  typedef base::OnceCallback<void(bool status)> CheckUrlCallback;

  WebRiskStore(base::FilePath file_path);
  virtual ~WebRiskStore();

  bool WriteToDisk(const ComputeThreatListDiffResponse& file_format);
  bool IsHashPrefixListEmptyOrExpired();
  bool IsHashPrefixAvailable(const std::string& hash_prefix);
  base::TimeDelta GetFirstUpdateTime();
  base::TimeDelta GetNextUpdateTime(const std::string& recommended_time);

 protected:
  friend class RefCounted<WebRiskStore>;

 private:
  bool ReadFromDisk();
  void FillHashPrefixListFromRawHashes(const std::string& raw_hashes);

  base::FilePath file_path_;
  std::vector<std::string> hash_prefix_list_;
  base::TimeDelta update_time_;
};

}  // namespace webrisk

#endif  // EXTENSIONS_SHELL_NEVA_WEBRISK_CORE_WEBRISK_STORE_H_
