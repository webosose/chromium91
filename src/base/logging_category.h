// Copyright 2021 LG Electronics, Inc.
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
#ifndef BASE_LOG_CATEGORY_H_
#define BASE_LOG_CATEGORY_H_

#include <cstddef>
#include <string>

#include "base/auto_reset.h"
#include "base/logging.h"

#define CATEGORY_LOG_STREAM(severity, category)                                \
  ::logging::CategoryLogMessage(__FILE__, __LINE__, ::logging::LOG_##severity, \
                                ::logging::LOG_CATEGORY_##category)            \
      .stream()

#define LOG_WITH_CATEGORY(severity, category)                     \
  LAZY_STREAM(                                                    \
      CATEGORY_LOG_STREAM(severity, category),                    \
      (LOG_IS_ON(severity) ||                                     \
       ::logging::IsLogCategoryEnabled(::logging::LOG_##severity, \
                                       ::logging::LOG_CATEGORY_##category)))

// LOG_WITH_CATEGORY macro enables sub-component marked logging so that
// underlayer logging system can control enabling logging based on the
// sub-components. For example, logging INFO level message with graphic module
// LOG_WITH_CATEGORY(INFO, GRAPHICS) << "Logging for graphics sub component"
// This log can be out to both of chromium logging and category log handler.

// clang-format off
#define LOG_CATEGORY(category) \
  category(LOG) \
  category(JSCONSOLE) \
  category(MAX)
// clang-format on

namespace logging {

bool IsLogCategoryEnabled(int severity, uint32_t category);
uint32_t GetLogCategoryForCurrentThread();

#define CREATE_LOG_CATEGORY_ENUM(category) LOG_CATEGORY_##category,
enum LogCategory { LOG_CATEGORY(CREATE_LOG_CATEGORY_ENUM) };

class CategoryLogMessage {
 public:
  CategoryLogMessage(const char* file,
                     int line,
                     LogSeverity severity,
                     uint32_t category);

  std::ostream& stream() { return log_message_.stream(); }

  virtual ~CategoryLogMessage();

 private:
  base::AutoReset<uint32_t> auto_reset_category_;
  LogMessage log_message_;
};

}  // namespace logging

#endif  // BASE_LOG_CATEGORY_H_
