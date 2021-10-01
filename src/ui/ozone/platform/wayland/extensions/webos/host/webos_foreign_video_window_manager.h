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

#ifndef UI_OZONE_PLATFORM_WAYLAND_EXTENSIONS_WEBOS_HOST_WEBOS_FOREIGN_VIDEO_WINDOW_MANAGER_H_
#define UI_OZONE_PLATFORM_WAYLAND_EXTENSIONS_WEBOS_HOST_WEBOS_FOREIGN_VIDEO_WINDOW_MANAGER_H_

#include <wayland-client.h>
#include <wayland-webos-foreign-client-protocol.h>

#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "base/single_thread_task_runner.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/ozone/common/neva/video_window_provider_delegate.h"
#include "ui/ozone/platform/wayland/extensions/webos/host/webos_foreign_video_window.h"
#include "ui/ozone/platform/wayland/host/wayland_connection.h"

namespace ui {

class WebOSForeignVideoWindowManager : public VideoWindowProviderDelegate {
 public:
  explicit WebOSForeignVideoWindowManager(WaylandConnection* connection,
                                          wl_webos_foreign* webos_foreign);
  WebOSForeignVideoWindowManager(const WebOSForeignVideoWindowManager&) =
      delete;
  WebOSForeignVideoWindowManager& operator=(
      const WebOSForeignVideoWindowManager&) = delete;

  ~WebOSForeignVideoWindowManager() override;

  static void HandleExportedWindowAssigned(
      void* data,
      struct wl_webos_exported* webos_exported,
      const char* window_id,
      uint32_t exported_type);

  gfx::Rect GetOwnerWindowBounds(gfx::AcceleratedWidget widget);

  gfx::Rect GetPrimaryScreenRect();

  scoped_refptr<base::SingleThreadTaskRunner> GetTaskRunner();

  void OnForeignWindowCreated(struct wl_webos_exported* webos_exported,
                              const std::string& native_window_id);

  void OnForeignWindowDestroyed(const base::UnguessableToken& window_id);

  void Flush();

  // Implements VideoWindowProviderDelegate
  void CreateVideoWindow(gfx::AcceleratedWidget widget,
                         const base::UnguessableToken& window_id) override;

  void DestroyVideoWindow(const base::UnguessableToken& window_id) override;

 private:
  void NotifyForeignWindowCreated(bool success,
                                  const base::UnguessableToken& window_id,
                                  ui::VideoWindow* video_window);

  const scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  WaylandConnection* connection_ = nullptr;
  struct wl_webos_foreign* webos_foreign_ = nullptr;
  std::vector<std::unique_ptr<WebOSForeignVideoWindow>> video_windows_;

  base::WeakPtr<WebOSForeignVideoWindowManager> weak_this_;
  base::WeakPtrFactory<WebOSForeignVideoWindowManager> weak_factory_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_WAYLAND_EXTENSIONS_WEBOS_HOST_WEBOS_FOREIGN_VIDEO_WINDOW_MANAGER_H_
