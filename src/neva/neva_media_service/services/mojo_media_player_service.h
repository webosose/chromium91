// Copyright 2019-2020 LG Electronics, Inc.
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

#ifndef NEVA_NEVA_MEDIA_SERVICE_SERVICES_MOJO_MEDIA_PLAYER_SERVICE_H_
#define NEVA_NEVA_MEDIA_SERVICE_SERVICES_MOJO_MEDIA_PLAYER_SERVICE_H_

#include <map>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "media/mojo/mojom/neva/media_types_neva.mojom.h"
#include "media/neva/media_player_neva_interface.h"
#include "media/neva/media_track_info.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "neva/neva_media_service/public/mojom/media_player.mojom.h"
#include "url/gurl.h"

namespace neva_media {

class MojoMediaPlayerService : public mojom::MediaPlayer,
                               public media::MediaPlayerNevaClient {
 public:
  MojoMediaPlayerService(media::MediaPlayerType media_player_type,
                         const std::string& app_id);
  ~MojoMediaPlayerService() override;

  void Connect(ConnectCallback callback) override;

  //-----------------------------------------------------------------
  // mojom::MediaPlayer implementations
  void Initialize(const bool is_video,
                  const double current_time,
                  const std::string& url,
                  const std::string& mime,
                  const std::string& referrer,
                  const std::string& user_agent,
                  const std::string& cookies,
                  const std::string& media_option,
                  const std::string& custom_option) override;
  void Start() override;
  void Pause() override;
  void Seek(const base::TimeDelta time) override;
  void SetRate(double rate) override;
  void SetVolume(double volume) override;
  void SetPoster(const GURL& poster) override;
  void SetPreload(media::mojom::Preload preload) override;
  void IsPreloadable(const std::string& content_media_option,
                     IsPreloadableCallback callback) override;
  void HasVideo(HasVideoCallback callback) override;
  void HasAudio(HasAudioCallback callback) override;
  void SelectTrack(const media::MediaTrackType type,
                   const std::string& id) override;
  void UsesIntrinsicSize(UsesIntrinsicSizeCallback callback) override;
  void MediaId(MediaIdCallback callback) override;
  void Suspend(media::SuspendReason reason) override;
  void Resume() override;
  void RequireMediaResource(RequireMediaResourceCallback callback) override;
  void IsRecoverableOnResume(IsRecoverableOnResumeCallback callback) override;
  void SetDisableAudio(bool disable) override;
  void SetMediaLayerId(const std::string& media_layer_id) override;
  void GetBufferedTimeRanges(GetBufferedTimeRangesCallback callback) override;
  void Send(const std::string& message, SendCallback callback) override;

  //-----------------------------------------------------------------
  // media::MediaPlayerNevaClient implementations
  void OnMediaMetadataChanged(base::TimeDelta duration,
                              const gfx::Size& coded_size,
                              const gfx::Size& natural_size,
                              bool success) override;
  void OnLoadComplete() override;
  void OnPlaybackComplete() override;
  void OnSeekComplete(const base::TimeDelta& current_time) override;
  void OnMediaError(int error) override;
  void OnVideoSizeChanged(const gfx::Size& coded_size,
                          const gfx::Size& natural_size) override;
  void OnMediaPlayerPlay() override;
  void OnMediaPlayerPause() override;
  void OnCustomMessage(const media::MediaEventType media_event_type,
                       const std::string& detail) override;
  void OnBufferingStateChanged(
      const media::BufferingState buffering_state) override;
  void OnAudioTracksUpdated(
      const std::vector<media::MediaTrackInfo>& audio_track_info) override;
  void OnTimeUpdate(base::TimeDelta current_timestamp,
                    base::TimeTicks current_time_ticks) override;
  void OnActiveRegionChanged(const gfx::Rect& active_region) override;

 private:
  std::vector<neva_media::mojom::TimeDeltaPairPtr> GetBufferedTimeRangesVector()
      const;
  mojo::AssociatedRemote<mojom::MediaPlayerListener> remote_client_;
  std::unique_ptr<media::MediaPlayerNeva> media_player_neva_;

  base::WeakPtrFactory<MojoMediaPlayerService> weak_factory_{this};
  MojoMediaPlayerService(const MojoMediaPlayerService&) = delete;
  MojoMediaPlayerService& operator=(const MojoMediaPlayerService&) = delete;
};

}  // namespace neva_media

#endif  // NEVA_NEVA_MEDIA_SERVICE_SERVICES_MOJO_MEDIA_PLAYER_SERVICE_H_
