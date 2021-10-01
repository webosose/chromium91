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

#ifndef UI_GFX_NEVA_VIDEO_UTILS_H_
#define UI_GFX_NEVA_VIDEO_UTILS_H_

#include "base/optional.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/gfx_export.h"

namespace gfx {

GFX_EXPORT void ComputeVideoHoleDisplayRect(
    gfx::Rect& source_rect,
    gfx::Rect& dest_rect,
    base::Optional<gfx::Rect>& ori_rect,
    const base::Optional<gfx::Size>& natural_video_size,
    const gfx::Rect& screen_rect);

}  // namespace gfx

#endif  // UI_GFX_NEVA_VIDEO_UTILS_H_