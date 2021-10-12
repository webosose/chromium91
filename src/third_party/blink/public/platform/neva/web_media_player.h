// Copyright 2017-2018 LG Electronics, Inc.
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

#ifndef THIRD_PARTY_BLINK_PUBLIC_PLATFORM_NEVA_WEB_MEDIA_PLAYER_H_
#define THIRD_PARTY_BLINK_PUBLIC_PLATFORM_NEVA_WEB_MEDIA_PLAYER_H_

#include "base/optional.h"
#include "third_party/blink/public/platform/web_string.h"
#include "ui/gfx/geometry/rect.h"

namespace blink {
namespace neva {

class WebMediaPlayer {
 public:
  enum RenderMode {
    RenderModeNone,
    RenderModeHole,
    RenderModeTexture,
    RenderModeDefault = RenderModeHole,
  };

// Originally defined in blink::WebMediaPlayer.
  enum Preload {
    kPreloadNone,
    kPreloadMetaData,
    kPreloadAuto,
  };

  enum MediaEventType {
    kMediaEventNone,
    kMediaEventUpdateUMSMediaInfo,
    kMediaEventBroadcastErrorMsg,
    kMediaEventDvrErrorMsg,
    kMediaEventUpdateCameraState,
    kMediaEventPipelineStarted,
  };

  // Returns the 'timeline offset' as defined in the HTML5 spec
  // (http://www.w3.org/html/wg/drafts/html/master/embedded-content.html#timeline-offset).
  // The function should return the number of milliseconds between the
  // 'timeline offset' and January 1, 1970 UTC. Use base::Time::ToJsTime() for
  // generate this value from Chromium code. If the content does not have a
  // 'timeline offset' then std::numeric_limits<double>::quiet_NaN() should be
  // returned.
  virtual double TimelineOffset() const {
    return std::numeric_limits<double>::quiet_NaN();
  }
  virtual void UpdateVideo(const gfx::Rect&, bool) {}
  virtual bool UsesIntrinsicSize() const { return true; }
  virtual WebString MediaId() const { return WebString(); }
  virtual bool HasAudioFocus() const { return false; }
  virtual void SetAudioFocus(bool focus) {}
  virtual void SetRenderMode(RenderMode mode) {}
  virtual void SetDisableAudio(bool disable) {}
  virtual void Suspend() {}
  virtual void OnMediaActivationPermitted() {}
  virtual void OnMediaPlayerObserverConnectionEstablished() {}
  // Send custom commands to platform pipeline or player. For example we can
  // send now subtitle related message to UMediaServer. Command are in JSON
  // format of 'command' and 'Parameter' pair. Command describes type of
  // message like 'SetInternalSubtitle' and Parameter describes value of
  // attributes like 'index'.
  virtual bool Send(const std::string& message) { return false; }

protected:
  struct PendingRequest {
    base::Optional<bool> pending_load_;
    base::Optional<Preload> pending_preload_;
    base::Optional<bool> pending_play_;
    // Seek gets pending if another seek is in progress or has no permit. Only
    // last pending seek will have effect.
    base::Optional<base::TimeDelta> pending_seek_time_;
    base::Optional<double> pending_rate_;
    base::Optional<double> pending_volume_;
  };
};

}  // namespace neva
}  // namespace blink

#endif  // THIRD_PARTY_BLINK_PUBLIC_PLATFORM_NEVA_WEB_MEDIA_PLAYER_H_
