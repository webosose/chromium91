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

#include "ozone/wayland/foreign_window/foreign_video_window_manager.h"

#include "base/logging.h"
#include "ozone/wayland/display.h"
#include "ozone/wayland/screen.h"
#include "ozone/wayland/shell/shell_surface.h"
#include "ozone/wayland/window.h"

namespace ozonewayland {

ForeignVideoWindowManager::ForeignVideoWindowManager(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : task_runner_(task_runner), weak_factory_(this) {
  VLOG(1) << __func__;
  weak_this_ = weak_factory_.GetWeakPtr();
}

ForeignVideoWindowManager::~ForeignVideoWindowManager() {
  VLOG(1) << __func__;
}

// static
void ForeignVideoWindowManager::HandleExportedWindowAssigned(
    void* data,
    struct wl_webos_exported* webos_exported,
    const char* native_window_id,
    uint32_t exported_type) {
  ForeignVideoWindowManager* manager =
      static_cast<ForeignVideoWindowManager*>(data);
  if (!manager)
    return;

  // ForeignVideoWindowManager exists as long as ozone is alive.
  // |native_window_id| should be converted to string before posting.
  manager->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(&ForeignVideoWindowManager::OnForeignWindowCreated,
                     base::Unretained(manager), webos_exported,
                     std::string(native_window_id)));
}

gfx::Rect ForeignVideoWindowManager::GetOwnerWindowBounds(
    gfx::AcceleratedWidget widget) {
  auto display = ozonewayland::WaylandDisplay::GetInstance();
  auto window = display->GetWindow(static_cast<unsigned>(widget));
  if (!window) {
    LOG(ERROR) << __func__ << " window is nullptr";
    return gfx::Rect();
  }

  return window->GetBounds();
}

gfx::Rect ForeignVideoWindowManager::GetPrimaryScreenRect() {
  auto display = ozonewayland::WaylandDisplay::GetInstance();
  auto screen = display->PrimaryScreen();
  if (!screen) {
    LOG(ERROR) << __func__ << " primary screen is nullptr";
    return gfx::Rect();
  }
  return screen->Geometry();
}

scoped_refptr<base::SingleThreadTaskRunner>
ForeignVideoWindowManager::GetTaskRunner() {
  return task_runner_;
}

void ForeignVideoWindowManager::OnForeignWindowCreated(
    struct wl_webos_exported* webos_exported,
    const std::string& native_window_id) {
  VLOG(1) << __func__ << " native_window_id = " << native_window_id;

  auto it = std::find_if(
      video_windows_.begin(), video_windows_.end(),
      [&webos_exported](std::unique_ptr<ForeignVideoWindow>& video_window) {
        return video_window->GetWebOSExported() == webos_exported;
      });
  if (it == video_windows_.end()) {
    LOG(ERROR) << __func__
               << " failed to find window for exported = " << webos_exported
               << " native_id = " << native_window_id;
    return;
  }

  ForeignVideoWindow* video_window = (*it).get();
  video_window->SetNativeWindowId(native_window_id);

  NotifyForeignWindowCreated(true, video_window->GetWindowId(), video_window);
}

void ForeignVideoWindowManager::OnForeignWindowDestroyed(
    const base::UnguessableToken& window_id) {
  client_->OnVideoWindowDestroyed(window_id);
}

void ForeignVideoWindowManager::Flush() {
  ozonewayland::WaylandDisplay* display =
      ozonewayland::WaylandDisplay::GetInstance();

  if (display)
    display->FlushDisplay();
}

void ForeignVideoWindowManager::CreateVideoWindow(
    gfx::AcceleratedWidget widget,
    const base::UnguessableToken& window_id) {
  VLOG(1) << __func__;

  ozonewayland::WaylandDisplay* display =
      ozonewayland::WaylandDisplay::GetInstance();
  ozonewayland::WaylandWindow* wayland_window = nullptr;
  ozonewayland::WaylandShellSurface* shell_surface = nullptr;
  struct wl_surface* surface = nullptr;

  if (display)
    wayland_window = display->GetWindow(static_cast<unsigned>(widget));
  if (wayland_window)
    shell_surface = wayland_window->ShellSurface();
  if (shell_surface)
    surface = shell_surface->GetWLSurface();

  if (!surface) {
    LOG(ERROR) << __func__ << " Failed to get surface widget=" << widget
               << " display=" << display << " wayland_window=" << wayland_window
               << " shell_surface=" << shell_surface << " surface=" << surface;
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&ForeignVideoWindowManager::NotifyForeignWindowCreated,
                       weak_this_, false, window_id, nullptr));
    return;
  }

  static const wl_webos_exported_listener exported_listener{
      ForeignVideoWindowManager::HandleExportedWindowAssigned};
  uint32_t kTypeVideo = 0;
  wl_webos_exported* webos_exported = wl_webos_foreign_export_element(
      display->GetWebosForeign(), surface, kTypeVideo);
  if (!webos_exported) {
    LOG(ERROR) << __func__ << " failed to create webos_exported";
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&ForeignVideoWindowManager::NotifyForeignWindowCreated,
                       weak_this_, false, window_id, nullptr));
    return;
  }
  wl_webos_exported_add_listener(webos_exported, &exported_listener, this);

  std::unique_ptr<ForeignVideoWindow> video_window =
      std::make_unique<ForeignVideoWindow>(this, display->GetCompositor(),
                                           webos_exported);
  video_window->SetOwnerWidget(widget);
  video_window->SetWindowId(window_id);
  video_windows_.push_back(std::move(video_window));

  Flush();
}

void ForeignVideoWindowManager::DestroyVideoWindow(
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
      base::BindOnce(&ForeignVideoWindowManager::OnForeignWindowDestroyed,
                     weak_this_, window_id));
}

void ForeignVideoWindowManager::NotifyForeignWindowCreated(
    bool success,
    const base::UnguessableToken& window_id,
    ui::VideoWindow* video_window) {
  client_->OnVideoWindowCreated(success, window_id, video_window);
}

}  // namespace ozonewayland
