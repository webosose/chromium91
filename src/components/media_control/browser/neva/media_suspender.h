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

#ifndef COMPONENTS_MEDIA_CONTROL_BROWSER_NEVA_MEDIA_SUSPENDER_H_
#define COMPONENTS_MEDIA_CONTROL_BROWSER_NEVA_MEDIA_SUSPENDER_H_

#include "base/macros.h"
#include "content/public/browser/web_contents_observer.h"

namespace media_control {

// This class implements a suspend video mode for web applications.
// Video in background is enabled by default.
class MediaSuspender : public content::WebContentsObserver {
 public:
  // Observes WebContents.
  explicit MediaSuspender(content::WebContents* web_contents);

  ~MediaSuspender() override;

  MediaSuspender(const MediaSuspender&) = delete;
  MediaSuspender& operator=(const MediaSuspender&) = delete;

  // Sets if the web contents is allowed to suspend video playback
  // when app is in background or not.
  void SetBackgroundVideoPlaybackEnabled(bool enabled);

  bool background_video_playback_enabled() const {
    return is_background_video_playback_enabled_;
  }

 private:
  // content::WebContentsObserver implementation:
  void RenderFrameCreated(content::RenderFrameHost* render_frame_host) final;
  void RenderViewReady() final;

  // Blocks or unblocks the render process from playing video in background
  void UpdateBackgroundVideoPlaybackEnabledState();
  void UpdateRenderFrameBackgroundVideoPlaybackEnabledState(
      content::RenderFrameHost* render_frame_host);

  // While in background video playback should be disabled by default
  bool is_background_video_playback_enabled_ = false;
};

}  // namespace media_control

#endif  // COMPONENTS_MEDIA_CONTROL_BROWSER_NEVA_MEDIA_SUSPENDER_H_
