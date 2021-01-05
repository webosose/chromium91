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

#include "base/logging_category.h"

namespace {

thread_local uint32_t log_category_for_current_thread_ =
    logging::LOG_CATEGORY_LOG;

}  // namespace

namespace logging {

uint32_t GetLogCategoryForCurrentThread() {
  return log_category_for_current_thread_;
}

CategoryLogMessage::CategoryLogMessage(const char* file,
                                       int line,
                                       LogSeverity severity,
                                       uint32_t category)
    : auto_reset_category_(&log_category_for_current_thread_, category),
      log_message_(file, line, severity) {}

CategoryLogMessage::~CategoryLogMessage() = default;

}  // namespace logging
