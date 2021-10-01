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

#include "ui/gfx/neva/video_utils.h"
#include "base/logging.h"

namespace gfx {

void ComputeVideoHoleDisplayRect(
    gfx::Rect& source_rect,
    gfx::Rect& dest_rect,
    base::Optional<gfx::Rect>& ori_rect,
    const base::Optional<gfx::Size>& natural_video_size,
    const gfx::Rect& screen_rect) {
  // Adjust for original_rect/source/dest rect
  const gfx::Rect& original_rect =
      natural_video_size ? gfx::Rect(natural_video_size.value()) : screen_rect;

  gfx::Rect visible_rect = gfx::IntersectRects(dest_rect, screen_rect);

  DCHECK(visible_rect.width() != 0 && visible_rect.height() != 0);

  int source_x = visible_rect.x() - dest_rect.x();
  int source_y = visible_rect.y() - dest_rect.y();

  float scale_width =
      static_cast<float>(original_rect.width()) / dest_rect.width();
  float scale_height =
      static_cast<float>(original_rect.height()) / dest_rect.height();

  gfx::Rect scaled_source_rect(source_x, source_y, visible_rect.width(),
                               visible_rect.height());
  scaled_source_rect = gfx::ScaleToEnclosingRectSafe(scaled_source_rect,
                                                     scale_width, scale_height);

  // |scaled_source_rect| must be inside of original_rect.
  if (!original_rect.Contains(scaled_source_rect)) {
    LOG(ERROR) << __func__
               << " some part of source rect are outside of original rect."
               << "  original rect: " << original_rect.ToString()
               << "  / source rect: " << scaled_source_rect.ToString();
    scaled_source_rect.Intersect(original_rect);
  }

  source_rect = scaled_source_rect;
  dest_rect = visible_rect;
  ori_rect = original_rect;
}

}  // namespace gfx
