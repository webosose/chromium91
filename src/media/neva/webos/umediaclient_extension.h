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

#ifndef MEDIA_NEVA_WEBOS_UMEDIACLIENT_EXTENSION_H_
#define MEDIA_NEVA_WEBOS_UMEDIACLIENT_EXTENSION_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "media/neva/webos/webos_mediaclient.h"

namespace media {
class UMediaClientImpl;

// UMediaClientExtension provide interface to platform media resource manager
class UMediaClientExtension {
 public:
  static std::unique_ptr<UMediaClientExtension> Create(
      const base::WeakPtr<UMediaClientImpl>& umedia_client,
      const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner);

  virtual ~UMediaClientExtension() {}

  // Update subtitle information on video resume
  virtual void UpdateSubtitleIfNeeded() {}
  // Disable subtitle setting on video suspend
  virtual void SuspendSubtitleIfNeeded() {}
  // Enable subtitle setting on video resume
  virtual void ResumeSubtitleIfNeeded() {}

  // Send custom message to system media manager
  virtual bool SendCustomMessage(const std::string& message) { return true; }
};

}  // namespace media

#endif  // MEDIA_NEVA_WEBOS_UMEDIACLIENT_EXTENSION_H_
