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

#include "extensions/shell/neva/platform_language_listener.h"

#include "base/command_line.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/switches.h"
#include "neva/pal_service/pal_platform_factory.h"
#include "neva/pal_service/public/language_tracker_delegate.h"
#include "third_party/blink/public/mojom/renderer_preferences.mojom.h"

PlatformLanguageListener::PlatformLanguageListener(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents), weak_factory_(this) {
  base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  if (cmd->HasSwitch(extensions::switches::kWebOSLunaServiceName)) {
    const std::string& application_name =
        cmd->GetSwitchValueASCII(extensions::switches::kWebOSLunaServiceName);

      delegate_ = pal::PlatformFactory::Get()->CreateLanguageTrackerDelegate(
          application_name,
          base::BindRepeating(&PlatformLanguageListener::OnLanguageChanged,
                              weak_factory_.GetWeakPtr()));
      if (delegate_->GetStatus() !=
          pal::LanguageTrackerDelegate::Status::kSuccess)
        LOG(ERROR) << __func__ << "(): error during delegate creation";
  }
}

PlatformLanguageListener::~PlatformLanguageListener() = default;

void PlatformLanguageListener::OnLanguageChanged(
    const std::string& language_string) {
  content::WebContents* contents = web_contents();
  if (!contents)
    return;

  auto* renderer_prefs(contents->GetMutableRendererPrefs());
  if (renderer_prefs->accept_languages.compare(language_string)) {
    renderer_prefs->accept_languages = language_string;
    contents->SyncRendererPrefs();
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(PlatformLanguageListener)
