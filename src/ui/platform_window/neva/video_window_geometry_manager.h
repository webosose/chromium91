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

#ifndef UI_PLATFORM_WINDOW_NEVA_VIDEO_WINDOW_GEOMETRY_MANAGER_H_
#define UI_PLATFORM_WINDOW_NEVA_VIDEO_WINDOW_GEOMETRY_MANAGER_H_

#include "base/unguessable_token.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"

namespace ui {

class VideoWindowGeometryManager {
 public:
  virtual ~VideoWindowGeometryManager() {}

  virtual bool IsInitialized() = 0;

  virtual void NotifyVideoWindowGeometryChanged(
      gfx::AcceleratedWidget w,
      const base::UnguessableToken& window_id,
      const gfx::Rect& rect) = 0;

  virtual void BeginOverlayProcessor(gfx::AcceleratedWidget w) = 0;

  virtual void EndOverlayProcessor(gfx::AcceleratedWidget w) = 0;
};

}  // namespace ui

#endif  // UI_PLATFORM_WINDOW_NEVA_VIDEO_WINDOW_GEOMETRY_MANAGER_H_
