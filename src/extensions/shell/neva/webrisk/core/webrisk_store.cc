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

#include "extensions/shell/neva/webrisk/core/webrisk_store.h"

#include "base/base64.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/hash/hash.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/rand_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "content/public/browser/storage_partition.h"
#include "content/shell/common/shell_neva_switches.h"
#include "net/base/net_errors.h"

namespace webrisk {

namespace {

// File name to be used for webrisk data
const char kWebRiskStoreFileName[] = "webrisk.store";

const int64_t kDefaultUpdateInterval = 60 * 60;  // 1 Hr

}  // namespace

constexpr char WebRiskStore::kThreatTypeMalware[];
constexpr size_t WebRiskStore::kHashPrefixSize;
constexpr size_t WebRiskStore::kMaxWebRiskStoreSize;

WebRiskStore::WebRiskStore() : update_time_(base::TimeDelta()) {
  file_path_ = GetFilePath();
  ReadFromDisk();
}

WebRiskStore::~WebRiskStore() = default;

bool WebRiskStore::ReadFromDisk() {
  VLOG(2) << __func__;

  std::string compute_diff_response;
  if (!base::ReadFileToStringWithMaxSize(file_path_, &compute_diff_response,
                                         kMaxWebRiskStoreSize)) {
    return false;
  }

  if (compute_diff_response.empty())
    return false;

  ComputeThreatListDiffResponse file_format;
  if (!file_format.ParseFromString(compute_diff_response))
    return false;

  std::string raw_hashes, hash_list;
  // Need to make the Raw hashes list dynamic
  raw_hashes = file_format.additions().raw_hashes(0).raw_hashes();
  FillHashPrefixListFromRawHashes(raw_hashes);
  update_time_ = GetNextUpdateTime(file_format.recommended_next_diff());
  return true;
}

bool WebRiskStore::WriteToDisk(
    const ComputeThreatListDiffResponse& file_format) {
  VLOG(2) << __func__;

  ThreatEntryAdditions entry_additions = file_format.additions();
  std::string file_format_string;
  if (!file_format.SerializeToString(&file_format_string)) {
    VLOG(1) << "Unable to serialize the response string !! ";
    return false;
  }

  size_t written = base::WriteFile(file_path_, file_format_string.data(),
                                   file_format_string.size());

  if (file_format_string.size() != written) {
    base::DeleteFile(file_path_);
    VLOG(1) << " Wrote " << written << " byte(s) instead of "
            << file_format_string.size() << " to " << file_path_.value();
    return false;
  }

  std::string raw_hashes, hash_list;
  raw_hashes = file_format.additions().raw_hashes(0).raw_hashes();
  FillHashPrefixListFromRawHashes(raw_hashes);
  return true;
}

void WebRiskStore::FillHashPrefixListFromRawHashes(
    const std::string& raw_hashes) {
  std::string hash_list;
  base::Base64Decode(raw_hashes, &hash_list);
  hash_prefix_list_.clear();
  while (!hash_list.empty()) {
    std::string hash_prefix = hash_list.substr(0, kHashPrefixSize);
    hash_prefix_list_.push_back(hash_prefix);
    hash_list.erase(0, kHashPrefixSize);
  }
}

bool WebRiskStore::IsHashPrefixListEmptyOrExpired() {
  if (!hash_prefix_list_.empty()) {
    if (update_time_ <= base::TimeDelta())
      return true;

    VLOG(2) << __func__ << " next update time= " << update_time_;
    return false;
  }
  return true;
}

base::TimeDelta WebRiskStore::GetFirstUpdateTime() {
  if (IsHashPrefixListEmptyOrExpired())
    update_time_ = base::TimeDelta();
  return update_time_;
}

base::TimeDelta WebRiskStore::GetNextUpdateTime(
    const std::string& recommended_time) {
  base::Time update_time;
  base::Time::FromUTCString(recommended_time.c_str(), &update_time);
  base::TimeDelta next_update_time = update_time - base::Time::Now();
  int64_t next_update_seconds = next_update_time.InSeconds();
  int64_t new_update_seconds =
      std::max(next_update_seconds, kDefaultUpdateInterval);
  return base::TimeDelta::FromSeconds(new_update_seconds);
}

bool WebRiskStore::IsHashPrefixAvailable(const std::string& hash_prefix) {
  if (hash_prefix_list_.empty())
    return false;

  if (std::find(hash_prefix_list_.begin(), hash_prefix_list_.end(),
                hash_prefix) != hash_prefix_list_.end()) {
    VLOG(2) << __func__ << " Hash Prefix found!! ";
    return true;
  }

  return false;
}

const base::FilePath WebRiskStore::GetFilePath() {
  base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();
  base::FilePath file_path(cmd_line->GetSwitchValuePath(switches::kUserDataDir)
                               .AppendASCII(kWebRiskStoreFileName));
  VLOG(2) << __func__ << " file_path= " << file_path.value();
  return file_path;
}

}  // namespace webrisk
