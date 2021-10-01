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

#include "ozone/wayland/foreign_window/foreign_video_window.h"

#include "base/logging.h"
#include "ozone/wayland/foreign_window/foreign_video_window_manager.h"
#include "ui/gfx/neva/video_utils.h"

namespace ozonewayland {

ForeignVideoWindow::ForeignVideoWindow(ForeignVideoWindowManager* manager,
                                       struct wl_compositor* compositor,
                                       struct wl_webos_exported* webos_exported)
    : manager_(manager),
      compositor_(compositor),
      webos_exported_(webos_exported) {
  VLOG(1) << " manager= " << manager_ << " / compositor= " << compositor
          << " / webos_exported=" << webos_exported_;
}
ForeignVideoWindow::~ForeignVideoWindow() {
  VLOG(1) << __func__;
  if (!webos_exported_)
    return;

  wl_webos_exported_destroy(webos_exported_);
}

void ForeignVideoWindow::SetNativeWindowId(
    const std::string& native_window_id) {
  native_window_id_ = native_window_id;
}

wl_webos_exported* ForeignVideoWindow::GetWebOSExported() {
  return webos_exported_;
}

std::string ForeignVideoWindow::GetNativeWindowId() {
  return native_window_id_;
}

void ForeignVideoWindow::UpdateGeometry(
    const gfx::Rect& src_rect,
    const gfx::Rect& dst_rect,
    const base::Optional<gfx::Rect>& ori_rect,
    const base::Optional<gfx::Size>& video_size) {
  gfx::Rect src = src_rect;
  gfx::Rect dst = dst_rect;
  base::Optional<gfx::Rect> ori = ori_rect;

  // set_exported_window is not work correctly with punch-through in below cases
  // 1. only part of dst video is located in the window
  // 2. the ratio of video width/height is different from the ratio of dst rect
  //    width/height
  // So we will use set_crop_region basically for the general cases.
#if defined(USE_GST_MEDIA)
  // When we are using texture mode, we should use set_exported_window only.
  bool use_set_crop_region = false;
#else
  bool use_set_crop_region = true;
#endif

  // Always use set_exported_window to keep the original video w/h ratio in
  // fullscreen.
  // set_exported_window always keeps the ratio even though dst is not match
  // with the ratio.
  // In webOS, for application window resolution is less than the screen
  // resolution, we have to consider window bounds to decide full-screen mode
  // of video.
  bool fullscreen = (manager_->GetOwnerWindowBounds(GetOwnerWidget()) == dst);
  if (!fullscreen && use_set_crop_region) {
    // TODO(neva): Currently it considers only single screen for
    // use_set_crop_region. If moving on supporting multi-screen we need to
    // check how to use set_crop_region with multi-screen and revisit this
    // clipping implementaion.
    gfx::Rect screen_rect = manager_->GetPrimaryScreenRect();

    gfx::ComputeVideoHoleDisplayRect(src, dst, ori, video_size, screen_rect);
  }

  wl_region* source_region = wl_compositor_create_region(compositor_);
  wl_region_add(source_region, src.x(), src.y(), src.width(), src.height());

  wl_region* dest_region = wl_compositor_create_region(compositor_);
  wl_region_add(dest_region, dst.x(), dst.y(), dst.width(), dst.height());

  if (ori) {
    wl_region* ori_region = wl_compositor_create_region(compositor_);
    wl_region_add(ori_region, ori->x(), ori->y(), ori->width(), ori->height());
    wl_webos_exported_set_crop_region(webos_exported_, ori_region,
                                      source_region, dest_region);
    wl_region_destroy(ori_region);
    VLOG(1) << __func__ << " called set_crop_region ori=" << ori->ToString()
            << " src=" << src.ToString() << " dst=" << dst.ToString();
  } else {
    wl_webos_exported_set_exported_window(webos_exported_, source_region,
                                          dest_region);
    VLOG(1) << __func__ << " called exported_window ori="
            << " src=" << src.ToString() << " dst=" << dst.ToString();
  }

  wl_region_destroy(dest_region);
  wl_region_destroy(source_region);

  manager_->Flush();
}

void ForeignVideoWindow::SetProperty(const std::string& name,
                                     const std::string& value) {
  wl_webos_exported_set_property(webos_exported_, name.c_str(), value.c_str());
  manager_->Flush();
}

void ForeignVideoWindow::SetVisibility(bool visibility) {
  if (visibility)
    SetProperty("mute", "off");
  else
    SetProperty("mute", "on");
}

}  // namespace ozonewayland
