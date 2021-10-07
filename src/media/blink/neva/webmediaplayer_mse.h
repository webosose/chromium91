// Copyright 2015-2020 LG Electronics, Inc.
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

#ifndef MEDIA_BLINK_NEVA_WEBMEDIAPLAYER_MSE_H_
#define MEDIA_BLINK_NEVA_WEBMEDIAPLAYER_MSE_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "media/base/media_log.h"
#include "media/blink/neva/video_frame_provider_impl.h"
#include "media/blink/neva/webmediaplayer_params_neva.h"
#include "media/blink/webmediaplayer_impl.h"
#include "media/neva/media_platform_api.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/blink/public/platform/web_media_player_source.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/platform_window/neva/mojom/video_window.mojom.h"

#if defined(USE_VIDEO_TEXTURE)
#include "ui/gl/neva/video_texture.h"
#endif

namespace media {

class MediaPlatformAPI;
class WebMediaPlayerImpl;
class VideoHoleGeometryUpdateHelper;

// The canonical implementation of blink::WebMediaPlayer that's backed by
// Pipeline. Handles normal resource loading, Media Source, and
// Encrypted Media.
class MEDIA_BLINK_EXPORT WebMediaPlayerMSE
    : public ui::mojom::VideoWindowClient,
      public WebMediaPlayerImpl {
 public:
  // Constructs a WebMediaPlayer implementation using Chromium's media stack.
  // |delegate| may be null. |renderer_factory_selector| must not be null.
  WebMediaPlayerMSE(
      blink::WebLocalFrame* frame,
      blink::WebMediaPlayerClient* client,
      blink::WebMediaPlayerEncryptedMediaClient* encrypted_client,
      blink::WebMediaPlayerDelegate* delegate,
      std::unique_ptr<media::RendererFactorySelector> renderer_factory_selector,
      UrlIndex* url_index,
      std::unique_ptr<VideoFrameCompositor> compositor,
      const StreamTextureFactoryCreateCB& stream_texture_factory_create_cb,
      std::unique_ptr<WebMediaPlayerParams> params,
      std::unique_ptr<WebMediaPlayerParamsNeva> params_neva);
  ~WebMediaPlayerMSE() override;

  static bool IsAvailable();

  blink::WebMediaPlayer::LoadTiming Load(
      LoadType load_type,
      const blink::WebMediaPlayerSource& source,
      CorsMode cors_mode,
      bool is_cache_disabled) override;

  void Play() override;

  void Pause() override;
  void Seek(double seconds) override;
  void SetRate(double rate) override;

  void SetVolume(double volume) override;

  void SetVolumeMultiplier(double multiplier) override;

  void SetContentDecryptionModule(
      blink::WebContentDecryptionModule* cdm,
      blink::WebContentDecryptionModuleResult result) override;

  // WebMediaPlayerDelegate::Observer interface stubs
  // TODO(neva): Below two methods changed to similar function name.
  //             Need to verify.
  void OnFrameHidden() override {}
  void OnFrameShown() override {}
  void OnIdleTimeout() override {}

  double TimelineOffset() const override;
  bool UsesIntrinsicSize() const override;
  void SetRenderMode(blink::WebMediaPlayer::RenderMode mode) override;
  void SetDisableAudio(bool disable) override;
  void Suspend() override;
  void OnMediaActivationPermitted() override;
  void OnMediaPlayerObserverConnectionEstablished() override;

  void Resume();
  void OnLoadPermitted();

  bool RenderTexture() const {
    return render_mode_ == blink::WebMediaPlayer::RenderModeTexture;
  }

  scoped_refptr<VideoFrame> GetCurrentFrameFromCompositor() const override;

 private:
  enum StatusOnSuspended {
    UnknownStatus = 0,
    PlayingStatus,
    PausedStatus,
  };

  void ProcessPendingRequests();
  void OnResumed();
  void OnSuspended();
  void OnVideoSizeChanged(const gfx::Size& coded_size,
                          const gfx::Size& natural_size);
  void OnError(PipelineStatus status) override;

  void OnMetadata(const PipelineMetadata& metadata) override;

  // Implements ui::mojom::VideoWindowClient
  void OnVideoWindowCreated(const ui::VideoWindowInfo& info) override;
  void OnVideoWindowDestroyed() override;
  void OnVideoWindowGeometryChanged(const gfx::Rect& rect) override;
  void OnVideoWindowVisibilityChanged(bool visibility) override;
  // End of ui::mojom::VideoWindowClient

  bool EnsureVideoWindowCreated();
  void ContinuePlayerWithWindowId();

  std::unique_ptr<VideoFrameProviderImpl> video_frame_provider_;
  const gfx::PointF additional_contents_scale_;
  std::string app_id_;
  bool is_suspended_ = false;
  StatusOnSuspended status_on_suspended_ = UnknownStatus;

  scoped_refptr<MediaPlatformAPI> media_platform_api_;

  // This value is updated by using value from media platform api.
  gfx::Size coded_size_;
  gfx::Size natural_size_;

  bool is_loading_ = false;
  LoadType pending_load_type_ = blink::WebMediaPlayer::kLoadTypeMediaSource;
  blink::WebMediaPlayerSource pending_source_;
  CorsMode pending_cors_mode_ = WebMediaPlayer::kCorsModeUnspecified;
  bool pending_is_cache_disabled_ = false;

  blink::WebMediaPlayer::RenderMode render_mode_ =
      blink::WebMediaPlayer::RenderModeNone;

  bool has_activation_permit_ = false;
  bool require_media_resource_ = true;

  PendingRequest pending_request_;

  WebMediaPlayerParamsNeva::CreateVideoWindowCB create_video_window_cb_;
  base::Optional<ui::VideoWindowInfo> video_window_info_ = base::nullopt;
  mojo::Remote<ui::mojom::VideoWindow> video_window_remote_;
  mojo::Receiver<ui::mojom::VideoWindowClient> video_window_client_receiver_{
      this};

  base::WeakPtr<WebMediaPlayerMSE> weak_this_for_mse_;
  base::WeakPtrFactory<WebMediaPlayerMSE> weak_factory_for_mse_{this};

  DISALLOW_COPY_AND_ASSIGN(WebMediaPlayerMSE);
};

}  // namespace media

#endif  // MEDIA_BLINK_NEVA_WEBMEDIAPLAYER_MSE_H_
