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

#include "components/media_control/browser/neva/media_suspender.h"

#include "components/media_control/mojom/media_playback_options.mojom.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"

namespace media_control {

MediaSuspender::MediaSuspender(content::WebContents* web_contents) {
  content::WebContentsObserver::Observe(web_contents);
}

MediaSuspender::~MediaSuspender() {}

void MediaSuspender::SetBackgroundVideoPlaybackEnabled(bool enabled) {
  if (is_background_video_playback_enabled_ == enabled)
    return;

  is_background_video_playback_enabled_ = enabled;
  UpdateBackgroundVideoPlaybackEnabledState();
}

void MediaSuspender::RenderFrameCreated(
    content::RenderFrameHost* render_frame_host) {
  UpdateRenderFrameBackgroundVideoPlaybackEnabledState(render_frame_host);
}

void MediaSuspender::RenderViewReady() {
  UpdateBackgroundVideoPlaybackEnabledState();
}

void MediaSuspender::UpdateBackgroundVideoPlaybackEnabledState() {
  if (!web_contents())
    return;

  const std::vector<content::RenderFrameHost*> frames =
      web_contents()->GetAllFrames();
  for (content::RenderFrameHost* frame : frames) {
    UpdateRenderFrameBackgroundVideoPlaybackEnabledState(frame);
  }
}

void MediaSuspender::UpdateRenderFrameBackgroundVideoPlaybackEnabledState(
    content::RenderFrameHost* render_frame_host) {
  DCHECK(render_frame_host);
  mojo::AssociatedRemote<components::media_control::mojom::MediaPlaybackOptions>
      media_playback_options;
  render_frame_host->GetRemoteAssociatedInterfaces()->GetInterface(
      &media_playback_options);

  media_playback_options->SetBackgroundVideoPlaybackEnabled(
      is_background_video_playback_enabled_);
}

}  // namespace media_control
