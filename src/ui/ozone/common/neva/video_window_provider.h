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

#ifndef UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_H_
#define UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_H_

#include "base/unguessable_token.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/platform_window/neva/mojom/video_window.mojom.h"
#include "ui/views/widget/desktop_aura/neva/ui_constants.h"

namespace ui {

class VideoWindowProvider {
 public:
  virtual ~VideoWindowProvider() {}

  virtual void CreateVideoWindow(
      gfx::AcceleratedWidget widget,
      const base::UnguessableToken& window_id,
      mojo::PendingRemote<mojom::VideoWindowClient> client,
      mojo::PendingReceiver<mojom::VideoWindow> receiver,
      const VideoWindowParams& params) = 0;

  virtual void DestroyVideoWindow(const base::UnguessableToken& window_id) = 0;

  virtual void VideoWindowGeometryChanged(
      const base::UnguessableToken& window_id,
      const gfx::Rect& dest_rect) = 0;

  virtual void VideoWindowVisibilityChanged(
      const base::UnguessableToken& window_id,
      bool visibility) = 0;

  virtual void OwnerWidgetStateChanged(gfx::AcceleratedWidget widget,
                                       WidgetState state) {}

  virtual void OwnerWidgetClosed(gfx::AcceleratedWidget widget) {}
};

}  // namespace ui

#endif  // UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_H_
