// Copyright 2022 LG Electronics, Inc.
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

#include "media/gpu/webos/webos_codec_utils.h"

#include "base/logging.h"

#include <libdrm/drm_fourcc.h>

namespace media {

uint32_t MCILPixelFormatToDrmFormat(uint32_t format) {
  switch (format) {
    case mcil::PIXEL_FORMAT_NV12:
      return DRM_FORMAT_NV12;
    case mcil::PIXEL_FORMAT_I420:
      return DRM_FORMAT_YUV420;
    case mcil::PIXEL_FORMAT_YV12:
      return DRM_FORMAT_YVU420;
    case mcil::PIXEL_FORMAT_BGRA:
      return DRM_FORMAT_ARGB8888;
    default:
      break;
  }
  return 0;
}

VideoPixelFormat
VideoPixelFormatFrom(mcil::VideoPixelFormat pix_format) {
  if (pix_format > mcil::PIXEL_FORMAT_UNKNOWN &&
      pix_format < mcil::PIXEL_FORMAT_MAX)
    return static_cast<VideoPixelFormat>(pix_format);
  return PIXEL_FORMAT_UNKNOWN;
}

VideoCodecProfile
VideoCodecProfileFrom(mcil::VideoCodecProfile profile) {
  if (profile > mcil::VIDEO_CODEC_PROFILE_UNKNOWN &&
      profile < mcil::VIDEO_CODEC_PROFILE_MAX)
    return static_cast<VideoCodecProfile>(profile);
  return VIDEO_CODEC_PROFILE_UNKNOWN;
}

base::Optional<VideoFrameLayout> VideoFrameLayoutFrom(
    mcil::scoped_refptr<mcil::VideoFrame> video_frame) {
  VLOG(2) << __func__;

  if (!video_frame)
    return base::nullopt;

  const size_t num_color_planes = video_frame->color_planes.size();
  const VideoPixelFormat video_format =
      VideoPixelFormatFrom(video_frame->format);

  std::vector<ColorPlaneLayout> planes;
  planes.reserve(num_color_planes);
  for (size_t i = 0; i < num_color_planes; ++i) {
    const mcil::ColorPlane color_plane = video_frame->color_planes[i];
    planes.emplace_back(color_plane.stride, color_plane.offset,
                        color_plane.size);
  }

  gfx::Size coded_size(video_frame->coded_size.width,
                       video_frame->coded_size.height);
  constexpr size_t buffer_alignment = 0x1000;
  if (!video_frame->is_multi_planar) {
    return VideoFrameLayout::CreateWithPlanes(video_format,
        coded_size, std::move(planes), buffer_alignment);
  } else {
    return VideoFrameLayout::CreateMultiPlanar(video_format,
        coded_size, std::move(planes), buffer_alignment);
  }
}

}  // namespace media
