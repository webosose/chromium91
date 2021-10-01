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

#ifndef UI_OZONE_PLATFORM_WAYLAND_EXTENSIONS_WEBOS_HOST_WEBOS_FOREIGN_VIDEO_WINDOW_H_
#define UI_OZONE_PLATFORM_WAYLAND_EXTENSIONS_WEBOS_HOST_WEBOS_FOREIGN_VIDEO_WINDOW_H_

#include <wayland-client.h>
#include <wayland-webos-foreign-client-protocol.h>

#include "base/optional.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/ozone/common/neva/video_window.h"
#include "ui/ozone/platform/wayland/common/wayland_object.h"

namespace ui {

class WebOSForeignVideoWindowManager;

class WebOSForeignVideoWindow : public ui::VideoWindow {
 public:
  explicit WebOSForeignVideoWindow(WebOSForeignVideoWindowManager* manager,
                                   struct wl_compositor* compositor,
                                   struct wl_webos_exported* webos_exported);
  WebOSForeignVideoWindow(const WebOSForeignVideoWindow&) = delete;
  WebOSForeignVideoWindow& operator=(const WebOSForeignVideoWindow&) = delete;

  ~WebOSForeignVideoWindow() override;

  void SetNativeWindowId(const std::string& native_window_id);

  wl_webos_exported* GetWebOSExported();

  // Implements ui::VideoWindow
  std::string GetNativeWindowId() override;

  void UpdateGeometry(const gfx::Rect& src_rect,
                      const gfx::Rect& dst_rect,
                      const base::Optional<gfx::Rect>& ori_rect,
                      const base::Optional<gfx::Size>& video_size) override;

  void SetProperty(const std::string& name, const std::string& value) override;

  void SetVisibility(bool visibility) override;

 private:
  WebOSForeignVideoWindowManager* manager_ = nullptr;
  struct wl_compositor* compositor_ = nullptr;
  struct wl_webos_exported* webos_exported_ = nullptr;
  std::string native_window_id_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_WAYLAND_EXTENSIONS_WEBOS_HOST_WEBOS_FOREIGN_VIDEO_WINDOW_H_
