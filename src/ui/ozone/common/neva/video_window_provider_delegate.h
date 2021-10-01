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

#ifndef UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_DELEGATE_H_
#define UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_DELEGATE_H_

#include "base/unguessable_token.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/ozone/common/neva/video_window.h"
#include "ui/platform_window/neva/video_window_info.h"

namespace ui {

class VideoWindowProviderDelegate {
 public:
  class Client {
   public:
    // Ownership of |video_window| should be held by
    // VideoWindowProviderDelegate.
    virtual void OnVideoWindowCreated(bool success,
                                      const base::UnguessableToken& window_id,
                                      VideoWindow* video_window) = 0;

    virtual void OnVideoWindowDestroyed(
        const base::UnguessableToken& window_id) = 0;
  };

  virtual ~VideoWindowProviderDelegate() {}

  void SetClient(Client* client) { client_ = client; }

  virtual void CreateVideoWindow(gfx::AcceleratedWidget widget,
                                 const base::UnguessableToken& window_id) = 0;

  virtual void DestroyVideoWindow(const base::UnguessableToken& window_id) = 0;

 protected:
  Client* client_ = nullptr;
};

}  // namespace ui

#endif  // UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_DELEGATE_H_
