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

#include "ui/ozone/platform/wayland/extensions/webos/host/webos_foreign_video_window_manager.h"

#include "base/logging.h"
#include "ui/ozone/platform/wayland/host/wayland_output_manager.h"
#include "ui/ozone/platform/wayland/host/wayland_window.h"

namespace ui {

WebOSForeignVideoWindowManager::WebOSForeignVideoWindowManager(
    WaylandConnection* connection,
    wl_webos_foreign* webos_foreign)
    : task_runner_(base::ThreadTaskRunnerHandle::Get()),
      connection_(connection),
      webos_foreign_(webos_foreign),
      weak_factory_(this) {
  VLOG(1) << __func__;
  weak_this_ = weak_factory_.GetWeakPtr();
}

WebOSForeignVideoWindowManager::~WebOSForeignVideoWindowManager() {
  VLOG(1) << __func__;
}

// static
void WebOSForeignVideoWindowManager::HandleExportedWindowAssigned(
    void* data,
    struct wl_webos_exported* webos_exported,
    const char* native_window_id,
    uint32_t exported_type) {
  WebOSForeignVideoWindowManager* manager =
      static_cast<WebOSForeignVideoWindowManager*>(data);
  if (!manager)
    return;

  // WebOSForeignVideoWindowManager exists as long as ozone is alive.
  // |native_window_id| should be converted to string before posting.
  manager->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(&WebOSForeignVideoWindowManager::OnForeignWindowCreated,
                     base::Unretained(manager), webos_exported,
                     std::string(native_window_id)));
}

gfx::Rect WebOSForeignVideoWindowManager::GetOwnerWindowBounds(
    gfx::AcceleratedWidget widget) {
  auto window_manager = connection_->wayland_window_manager();
  auto wayland_window = window_manager->GetWindow(widget);

  if (!wayland_window) {
    LOG(ERROR) << __func__ << " wayland_window is nullptr";
    return gfx::Rect();
  }

  return wayland_window->GetBounds();
}

gfx::Rect WebOSForeignVideoWindowManager::GetPrimaryScreenRect() {
  auto output_manager = connection_->wayland_output_manager();
  auto screen = output_manager->wayland_screen();

  // TODO(neva): Consider use DisplayObserver for performance.
  auto primary_display = screen->GetPrimaryDisplay();

  return primary_display.bounds();
}

scoped_refptr<base::SingleThreadTaskRunner>
WebOSForeignVideoWindowManager::GetTaskRunner() {
  return task_runner_;
}

void WebOSForeignVideoWindowManager::OnForeignWindowCreated(
    struct wl_webos_exported* webos_exported,
    const std::string& native_window_id) {
  VLOG(1) << __func__ << " native_window_id = " << native_window_id;

  auto it =
      std::find_if(video_windows_.begin(), video_windows_.end(),
                   [&webos_exported](
                       std::unique_ptr<WebOSForeignVideoWindow>& video_window) {
                     return video_window->GetWebOSExported() == webos_exported;
                   });
  if (it == video_windows_.end()) {
    LOG(ERROR) << __func__
               << " failed to find window for exported = " << webos_exported
               << " native_id = " << native_window_id;
    return;
  }

  WebOSForeignVideoWindow* video_window = (*it).get();
  video_window->SetNativeWindowId(native_window_id);

  NotifyForeignWindowCreated(true, video_window->GetWindowId(), video_window);
}

void WebOSForeignVideoWindowManager::OnForeignWindowDestroyed(
    const base::UnguessableToken& window_id) {
  client_->OnVideoWindowDestroyed(window_id);
}

void WebOSForeignVideoWindowManager::Flush() {
  connection_->ScheduleFlush();
}

void WebOSForeignVideoWindowManager::CreateVideoWindow(
    gfx::AcceleratedWidget widget,
    const base::UnguessableToken& window_id) {
  VLOG(1) << __func__;

  auto window_manager = connection_->wayland_window_manager();
  auto wayland_window = window_manager->GetWindow(widget);
  // TODO(neva): Is this way correct?
  auto wayland_surface = wayland_window->root_surface();
  auto surface = wayland_surface->surface();

  if (!surface) {
    LOG(ERROR) << __func__ << " Failed to get surface widget=" << widget
               << " window_manager=" << window_manager
               << " wayland_window=" << wayland_window
               << " wayland_surface=" << wayland_surface
               << " surface=" << surface;
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(
            &WebOSForeignVideoWindowManager::NotifyForeignWindowCreated,
            weak_this_, false, window_id, nullptr));
    return;
  }

  static const wl_webos_exported_listener exported_listener{
      WebOSForeignVideoWindowManager::HandleExportedWindowAssigned};
  uint32_t kTypeVideo = 0;
  wl_webos_exported* webos_exported =
      wl_webos_foreign_export_element(webos_foreign_, surface, kTypeVideo);
  if (!webos_exported) {
    LOG(ERROR) << __func__ << " failed to create webos_exported";
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(
            &WebOSForeignVideoWindowManager::NotifyForeignWindowCreated,
            weak_this_, false, window_id, nullptr));
    return;
  }
  wl_webos_exported_add_listener(webos_exported, &exported_listener, this);

  std::unique_ptr<WebOSForeignVideoWindow> video_window =
      std::make_unique<WebOSForeignVideoWindow>(this, connection_->compositor(),
                                                webos_exported);
  video_window->SetOwnerWidget(widget);
  video_window->SetWindowId(window_id);
  video_windows_.push_back(std::move(video_window));

  Flush();
}

void WebOSForeignVideoWindowManager::DestroyVideoWindow(
    const base::UnguessableToken& window_id) {
  VLOG(1) << __func__;

  auto it = video_windows_.begin();
  for (auto& video_window : video_windows_) {
    if (video_window->GetWindowId() == window_id)
      break;
    it++;
  }

  if (it != video_windows_.end())
    video_windows_.erase(it);

  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&WebOSForeignVideoWindowManager::OnForeignWindowDestroyed,
                     weak_this_, window_id));
}

void WebOSForeignVideoWindowManager::NotifyForeignWindowCreated(
    bool success,
    const base::UnguessableToken& window_id,
    ui::VideoWindow* video_window) {
  client_->OnVideoWindowCreated(success, window_id, video_window);
}

}  // namespace ui
