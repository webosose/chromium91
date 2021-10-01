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

#ifndef UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_H_
#define UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_H_

#include "base/optional.h"
#include "base/unguessable_token.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"

namespace ui {

class VideoWindow {
 public:
  virtual ~VideoWindow() {}

  virtual std::string GetNativeWindowId() = 0;

  base::UnguessableToken GetWindowId() { return window_id_; }

  void SetWindowId(const base::UnguessableToken& window_id) {
    window_id_ = window_id;
  }

  gfx::AcceleratedWidget GetOwnerWidget() { return widget_; }

  void SetOwnerWidget(const gfx::AcceleratedWidget widget) { widget_ = widget; }

  virtual void UpdateGeometry(
      const gfx::Rect& src_rect,
      const gfx::Rect& dst_rect,
      const base::Optional<gfx::Rect>& ori_rect,
      const base::Optional<gfx::Size>& natural_video_size) = 0;

  virtual void SetProperty(const std::string& name,
                           const std::string& value) = 0;

  virtual void SetVisibility(bool visibility) = 0;

 protected:
  gfx::AcceleratedWidget widget_;
  base::UnguessableToken window_id_;
};

}  // namespace ui

#endif  // UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_H_
