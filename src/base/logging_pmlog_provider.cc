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

#include "base/logging_pmlog_provider.h"

#include <glib.h>

#include "base/json/string_escape.h"
#include "base/memory/singleton.h"
#include "base/strings/string_piece.h"
#include "base/threading/platform_thread.h"

namespace {

constexpr size_t kMaxLogLength = 896;
constexpr size_t kMaxFileTailSize = 20;
const char kDefaultMajorLogChannelName[] = "chromium";

const char* const kSeverityLabel[] = {"I", "W", "E", "F"};
static_assert(logging::LOGGING_NUM_SEVERITIES == base::size(kSeverityLabel),
              "Incorrect number of kSeverityLabel");

const char* log_severity_name(int severity) {
  if (severity >= 0 && severity < logging::LOGGING_NUM_SEVERITIES)
    return kSeverityLabel[severity];
  return "U";
}

};  // namespace

namespace logging {

bool IsLogCategoryEnabled(int severity, uint32_t category) {
  return PmLogProvider::IsLogCategoryEnabled(severity, category);
}

PmLogProvider::PmLogProvider() = default;

PmLogProvider* PmLogProvider::GetInstance() {
  return base::Singleton<
      PmLogProvider, base::StaticMemorySingletonTraits<PmLogProvider>>::get();
}

void PmLogProvider::Initialize(const char* major_name) {
  logging::LoggingSettings settings;
  logging::InitLogging(settings);
  logging::SetLogItems(true /* Process ID */, true /* Thread ID */,
                       true /* Timestamp */, false /* Tick count */);

  PmLogProvider* provider = PmLogProvider::GetInstance();
  if (!provider)
    return;

  provider->Register(major_name);

  // Register our message handler with logging.
  SetLogMessageHandler(LogMessage);
}

void PmLogProvider::Register(const char* major_name) {
  if (major_name == nullptr)
    major_name = kDefaultMajorLogChannelName;

  for (uint32_t category = LOG_CATEGORY_LOG; category < LOG_CATEGORY_MAX;
       category++) {
    std::stringstream ss;
    ss << major_name << "." << pmlog_context_names_[category];
    std::string context_name = ss.str();
    std::transform(context_name.begin(), context_name.end(),
                   context_name.begin(),
                   [](unsigned char c) { return std::tolower(c); }  // correct
    );
    PmLogGetContext(context_name.c_str(), &(pmlog_contexts_[category]));
  }
}

bool PmLogProvider::LogMessage(logging::LogSeverity severity,
                               const char* file,
                               int line,
                               size_t message_start,
                               const std::string& message) {
  uint32_t category = GetLogCategoryForCurrentThread();
  PmLogProvider* provider = GetInstance();
  if (provider == nullptr || category >= LOG_CATEGORY_MAX)
    return false;

  PmLogContext context = provider->pmlog_contexts_[category];

#define FOR_LEVELS_CALL()               \
  if (severity >= 0) {                  \
    switch (severity) {                 \
      case LOG_INFO:                    \
        PMLOG_CALL(Debug, NULL);        \
        break;                          \
      case LOG_WARNING:                 \
        PMLOG_CALL(Warning, "WARNING"); \
        break;                          \
      case LOG_ERROR:                   \
        PMLOG_CALL(Error, "ERROR");     \
        break;                          \
      case LOG_FATAL:                   \
        PMLOG_CALL(Critical, "FATAL");  \
        break;                          \
    }                                   \
  } else {                              \
    PMLOG_CALL(Debug, NULL);            \
  }

#define PMLOG_CALL(level_suffix, msgid)                     \
  if (!PmLogIsEnabled(context, kPmLogLevel_##level_suffix)) \
    return false;

  FOR_LEVELS_CALL();

#undef PMLOG_CALL

  base::StringPiece file_tail(file);
  if (file_tail.size() > kMaxFileTailSize)
    file_tail.remove_prefix(file_tail.size() - kMaxFileTailSize);

  std::string escaped_string = base::GetQuotedJSONString(
      base::StringPiece(message).substr(message_start));

#define PMLOG_CALL(level_suffix, msgid)                                 \
  if (severity >= 0) {                                                  \
    PmLogMsg(context, level_suffix, msgid, 0, "%s[%d:%d:%s(%d)] %.*s",  \
             log_severity_name(severity), getpid(),                     \
             base::PlatformThread::CurrentId(), file_tail.data(), line,  \
             std::min(escaped_string.length() - offset, kMaxLogLength), \
             escaped_string.c_str() + offset);                          \
  } else {                                                              \
    PmLogMsg(context, level_suffix, msgid, 0, "V%d[%d:%d:%s(%d)] %.*s", \
             -severity, getpid(), base::PlatformThread::CurrentId(),    \
             file_tail.data(), line,                                     \
             std::min(escaped_string.length() - offset, kMaxLogLength), \
             escaped_string.c_str() + offset);                          \
  }

  for (size_t offset = 0; offset < escaped_string.length();
       offset += kMaxLogLength) {
    FOR_LEVELS_CALL();
  }

#undef PMLOG_CALL

  // We keep regular logs working too.
  return false;
}

bool PmLogProvider::IsLogCategoryEnabled(int severity, uint32_t category) {
  PmLogProvider* provider = GetInstance();
  if (provider == nullptr || category >= LOG_CATEGORY_MAX)
    return false;

  PmLogContext context = provider->pmlog_contexts_[category];

#define PMLOG_CALL(level_suffix, msgid) \
  return PmLogIsEnabled(context, kPmLogLevel_##level_suffix)

  FOR_LEVELS_CALL();

#undef PMLOG_CALL

  // the return statement below is not executed. but, Added "return" to avoid
  // risk when FOR_LEVELS_CALL() changes.
  return false;
}

}  // namespace logging
