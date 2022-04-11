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

#ifndef MEDIA_GPU_WEBOS_WEBOS_CODEC_UTILS_H_
#define MEDIA_GPU_WEBOS_WEBOS_CODEC_UTILS_H_

#include <mcil/codec_types.h>
#include <mcil/video_frame.h>

#include "media/base/video_codecs.h"
#include "media/base/video_frame.h"
#include "media/base/video_types.h"

namespace media {

uint32_t MCILPixelFormatToDrmFormat(uint32_t format);

VideoPixelFormat VideoPixelFormatFrom(mcil::VideoPixelFormat pixel_format);

VideoCodecProfile VideoCodecProfileFrom(mcil::VideoCodecProfile profile);

base::Optional<VideoFrameLayout> VideoFrameLayoutFrom(
    mcil::scoped_refptr<mcil::VideoFrame> video_frame);

}  // namespace media

#endif  // MEDIA_GPU_WEBOS_WEBOS_CODEC_UTILS_H_