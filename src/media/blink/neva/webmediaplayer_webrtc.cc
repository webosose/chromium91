// Copyright 2020 LG Electronics, Inc.
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

#include "media/blink/neva/webmediaplayer_webrtc.h"

#include "cc/layers/layer.h"
#include "cc/layers/video_layer.h"
#include "cc/trees/layer_tree_host.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/media_util.h"
#include "media/blink/neva/stream_texture_interface.h"
#include "media/blink/neva/video_frame_provider_impl.h"
#include "media/neva/media_platform_api.h"
#include "media/neva/media_preferences.h"
#include "media/webrtc/neva/webrtc_pass_through_video_decoder.h"
#include "neva/logging.h"
#include "third_party/blink/public/platform/web_media_player_client.h"
#include "third_party/blink/public/web/modules/media/webmediaplayer_util.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/renderer/modules/mediastream/webmediaplayer_ms_compositor.h"

namespace media {

#define BIND_TO_RENDER_LOOP_VIDEO_FRAME_PROVIDER(function) \
  (DCHECK(main_task_runner_->BelongsToCurrentThread()),    \
   BindToCurrentLoop(                                      \
       base::Bind(function, (this->video_frame_provider_impl_->AsWeakPtr()))))

#define BIND_TO_RENDER_LOOP(function)                   \
  (DCHECK(main_task_runner_->BelongsToCurrentThread()), \
   media::BindToCurrentLoop(base::Bind(function, weak_ptr_this_)))

namespace {

// Any reasonable size, will be overridden by the decoder anyway.
const gfx::Size kDefaultSize(640, 480);

}  // namespace

WebMediaPlayerWebRTC::WebMediaPlayerWebRTC(
    blink::WebLocalFrame* frame,
    blink::WebMediaPlayerClient* client,
    blink::WebMediaPlayerDelegate* delegate,
    std::unique_ptr<media::MediaLog> media_log,
    scoped_refptr<base::SingleThreadTaskRunner> main_render_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> compositor_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> media_task_runner,
    scoped_refptr<base::TaskRunner> worker_task_runner,
    media::GpuVideoAcceleratorFactories* gpu_factories,
    const blink::WebString& sink_id,
    blink::CreateSurfaceLayerBridgeCB create_bridge_callback,
    std::unique_ptr<blink::WebVideoFrameSubmitter> submitter,
    blink::WebMediaPlayer::SurfaceLayerMode surface_layer_mode,
    const StreamTextureFactoryCreateCB& stream_texture_factory_create_cb,
    std::unique_ptr<WebMediaPlayerParamsNeva> params_neva)
    : blink::WebMediaPlayerMS(frame,
                              client,
                              delegate,
                              std::move(media_log),
                              main_render_task_runner,
                              io_task_runner,
                              compositor_task_runner,
                              media_task_runner,
                              worker_task_runner,
                              gpu_factories,
                              sink_id,
                              std::move(create_bridge_callback),
                              std::move(submitter),
                              surface_layer_mode),
      additional_contents_scale_(params_neva->additional_contents_scale()),
      main_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      app_id_(params_neva->application_id().Utf8()),
      create_media_platform_api_cb_(
          params_neva->override_create_media_platform_api()),
      create_video_window_cb_(params_neva->get_create_video_window_callback()),
      weak_factory_this_(this) {
  NEVA_LOGTF(INFO) << "delegate_id_: " << delegate_id_;

  weak_ptr_this_ = weak_factory_this_.GetWeakPtr();

  video_frame_provider_impl_ = std::make_unique<media::VideoFrameProviderImpl>(
      stream_texture_factory_create_cb, compositor_task_runner);
  video_frame_provider_impl_->SetWebLocalFrame(frame);
  video_frame_provider_impl_->SetWebMediaPlayerClient(client);

  base::Optional<bool> is_audio_disabled = client_->IsAudioDisabled();
  if (is_audio_disabled.has_value())
    SetDisableAudio(*is_audio_disabled);

  SetRenderMode(client_->RenderMode());

  // TODO(neva, sync-to-91):
  // Set to false for handling multiple streams. In WebRTC scenario,
  // multiple WebMediaPlayer for WebRTC may are created. And someone of them
  // actually doesn't decode frame(see |HandleVideoFrame()|).
  // But still, this way is risky when multiple remote streams for decoding
  // are provdied. But at the current moment there is no better way in our
  // architecture.
  require_media_resource_ = false;
}

WebMediaPlayerWebRTC::~WebMediaPlayerWebRTC() {
  NEVA_LOGTF(INFO) << "delegate_id_: " << delegate_id_;

  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  compositor_task_runner_->DeleteSoon(FROM_HERE,
                                      std::move(video_frame_provider_impl_));

  if (media_platform_api_)
    media_platform_api_->Finalize();
}

blink::WebMediaPlayer::LoadTiming WebMediaPlayerWebRTC::Load(
    LoadType load_type,
    const blink::WebMediaPlayerSource& source,
    CorsMode cors_mode,
    bool is_cache_disabled) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  DCHECK(source.IsMediaStream());

  is_loading_ = true;
  pending_load_type_ = load_type;
  pending_stream_ = source.GetAsMediaStream();
  pending_cors_mode_ = cors_mode;
  pending_is_cache_disabled_ = is_cache_disabled;

  client_->DidMediaActivationNeeded();

  return LoadTiming::kDeferred;
}

void WebMediaPlayerWebRTC::Play() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (!has_activation_permit_) {
    status_on_suspended_ = StatusOnSuspended::PlayingStatus;
    if (!client_->IsSuppressedMediaPlay())
      client_->DidMediaActivationNeeded();
    return;
  }
  blink::WebMediaPlayerMS::Play();
}

void WebMediaPlayerWebRTC::Pause() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (is_suspended_) {
    status_on_suspended_ = StatusOnSuspended::PausedStatus;
    return;
  }

  // call base-class implementation
  blink::WebMediaPlayerMS::Pause();

  paused_time_ = base::TimeDelta::FromMillisecondsD(CurrentTime());
}

void WebMediaPlayerWebRTC::SetRate(double rate) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (!has_activation_permit_) {
    if (!client_->IsSuppressedMediaPlay())
      client_->DidMediaActivationNeeded();
    return;
  }

  blink::WebMediaPlayerMS::SetRate(rate);
}

void WebMediaPlayerWebRTC::SetVolume(double volume) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  blink::WebMediaPlayerMS::SetVolume(volume);
}

void WebMediaPlayerWebRTC::OnFrameHidden() {
  NEVA_LOGTF(INFO) << ": delegate_id_: " << delegate_id_;

  blink::WebMediaPlayerMS::OnFrameHidden();

  SuspendInternal();
}

void WebMediaPlayerWebRTC::OnFrameShown() {
  NEVA_LOGTF(INFO) << ": delegate_id_: " << delegate_id_;

  blink::WebMediaPlayerMS::OnFrameShown();

  ResumeInternal();
}

void WebMediaPlayerWebRTC::OnMediaActivationPermitted() {
  // If we already have activation permit, just skip.
  if (has_activation_permit_) {
    client_->DidMediaActivated();
    return;
  }

  has_activation_permit_ = true;

  if (is_loading_) {
    OnLoadPermitted();
    return;
  }

  Play();

  client_->ResumePlayback();
  client_->DidMediaActivated();
}

void WebMediaPlayerWebRTC::OnMediaPlayerObserverConnectionEstablished() {
  client_->DidMediaCreated(require_media_resource_);
}

void WebMediaPlayerWebRTC::OnVideoWindowCreated(
    const ui::VideoWindowInfo& info) {
  NEVA_VLOGTF(1);
  video_window_info_ = info;
  video_frame_provider_impl_->SetOverlayPlaneId(info.window_id);
  if (media_platform_api_)
    media_platform_api_->SetMediaLayerId(info.native_window_id);
  if (!coded_size_.IsEmpty() || !natural_size_.IsEmpty())
    video_window_remote_->SetVideoSize(coded_size_, natural_size_);

  main_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&WebMediaPlayerWebRTC::ContinuePlayerWithWindowId,
                     weak_ptr_this_));
}

void WebMediaPlayerWebRTC::OnVideoWindowDestroyed() {
  NEVA_VLOGTF(1);
  video_window_info_ = base::nullopt;
  video_window_client_receiver_.reset();
}

void WebMediaPlayerWebRTC::OnVideoWindowGeometryChanged(const gfx::Rect& rect) {
}

void WebMediaPlayerWebRTC::OnVideoWindowVisibilityChanged(bool visibility) {}

void WebMediaPlayerWebRTC::SetRenderMode(
    blink::WebMediaPlayer::RenderMode mode) {
  if (render_mode_ == mode)
    return;

  render_mode_ = mode;

  if (is_render_mode_texture()) {
    video_frame_provider_impl_->SetFrameType(VideoFrameProviderImpl::kHole);
  } else {
#if defined(NEVA_VIDEO_HOLE)
    video_frame_provider_impl_->SetFrameType(VideoFrameProviderImpl::kHole);
#endif
  }
}

void WebMediaPlayerWebRTC::SetDisableAudio(bool disable) {
  if (media_platform_api_)
    media_platform_api_->SetDisableAudio(disable);
}

bool WebMediaPlayerWebRTC::HandleVideoFrame(
    const scoped_refptr<media::VideoFrame>& video_frame) {
  // For local stream video frames contains raw data in I420 format.
  // So decoding is not needed hence we return the same to parent
  // WebMediaPlayerMS class for rendering using chromium video layer.
  // For remote streams we pass the buffer to platform media pipeline
  // for decoding and rendering.
  if (!video_frame->metadata().codec_id.has_value()) {
    if (pipeline_running_ && media_platform_api_) {
      main_task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(&WebMediaPlayerWebRTC::ReleaseMediaPlatformAPI,
                         weak_ptr_this_));
    }
    return false;
  }

  if (software_fallback_callback_.is_null() &&
      !video_frame->metadata().software_fallback_callback.is_null()) {
    software_fallback_callback_ =
        video_frame->metadata().software_fallback_callback;
    if (pipeline_status_ == DECODER_ERROR_RESOURCE_IS_RELEASED ||
        pipeline_status_ == PIPELINE_ERROR_ABORT) {
      software_fallback_callback_.Run();
      return true;
    }
  }

  // The pipeline operation here might be incorrect, but encoded frame
  // couldn't be handled somewhere else.
  if (pipeline_status_ != media::PIPELINE_OK) {
    LOG(ERROR) << __func__ << " : pipeline_status error";
    return true;
  }
  codec_ = *(video_frame->metadata().codec_id);

  if (is_suspended_)
    return true;

  if (!has_first_frame_) {
    has_first_frame_ = true;
    handle_encoded_frames_ = true;
    EnqueueHoleFrame(video_frame);
  }

  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&WebMediaPlayerWebRTC::HandleEncodedFrame,
                                weak_ptr_this_, video_frame));
  return true;
}

void WebMediaPlayerWebRTC::TriggerResize() {
  if (handle_encoded_frames_) {
    gfx::Size size(NaturalSize());

    video_frame_provider_impl_->SetNaturalVideoSize(size);
    if (video_window_remote_)
      video_window_remote_->SetVideoSize(coded_size_, natural_size_);

    video_frame_provider_impl_->UpdateVideoFrame();
  }

  blink::WebMediaPlayerMS::TriggerResize();
}

void WebMediaPlayerWebRTC::OnFirstFrameReceived(
    media::VideoRotation video_rotation,
    bool is_opaque) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (handle_encoded_frames_) {
    if (is_loading_) {
      is_loading_ = false;
      client_->DidMediaActivated();
    }

    has_first_frame_ = true;

    OnRotationChanged(video_rotation);
    OnOpacityChanged(is_opaque);

    SetReadyState(WebMediaPlayer::kReadyStateHaveMetadata);
    SetReadyState(WebMediaPlayer::kReadyStateHaveEnoughData);

    TriggerResize();
    ResetCanvasCache();
    return;
  }

  blink::WebMediaPlayerMS::OnFirstFrameReceived(video_rotation, is_opaque);
}

void WebMediaPlayerWebRTC::OnRotationChanged(VideoRotation video_rotation) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (handle_encoded_frames_) {
    video_transformation_ = {video_rotation, 0};

    if (!bridge_) {
      // Keep the old |video_layer_| alive until SetCcLayer() is called with
      // a new pointer, as it may use the pointer from the last call.
      auto new_video_layer = cc::VideoLayer::Create(
          video_frame_provider_impl_.get(), video_rotation);
      get_client()->SetCcLayer(new_video_layer.get());
      video_layer_ = std::move(new_video_layer);
    }
    return;
  }

  blink::WebMediaPlayerMS::OnRotationChanged(video_rotation);
}

void WebMediaPlayerWebRTC::HandleEncodedFrame(
    const scoped_refptr<media::VideoFrame>& encoded_frame) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (!media_platform_api_) {
    StartMediaPipeline();
  }

  {
    base::AutoLock auto_lock(frame_lock_);

    // While pipeline is initializing all pending encoded frames
    // will be removed after receiving a new key frame.
    if (encoded_frame->metadata().key_frame == true && !pipeline_running_)
      pending_encoded_frames_.clear();
    pending_encoded_frames_.push_back(encoded_frame);
  }

  if (pipeline_running_) {
    media_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&WebMediaPlayerWebRTC::OnPipelineFeed, weak_ptr_this_));
    EnqueueHoleFrame(encoded_frame);
  }
}

void WebMediaPlayerWebRTC::StartMediaPipeline() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (media_platform_api_)
    return;

  // Create MediaPlatformAPI
  if (create_media_platform_api_cb_) {
    media_platform_api_ = create_media_platform_api_cb_.Run(
        media_task_runner_, client_->IsVideo(), app_id_,
        BIND_TO_RENDER_LOOP(&WebMediaPlayerWebRTC::OnVideoSizeChanged),
        BIND_TO_RENDER_LOOP(&WebMediaPlayerWebRTC::OnResumed),
        BIND_TO_RENDER_LOOP(&WebMediaPlayerWebRTC::OnSuspended),
        BIND_TO_RENDER_LOOP_VIDEO_FRAME_PROVIDER(
            &VideoFrameProviderImpl::ActiveRegionChanged),
        BIND_TO_RENDER_LOOP(&WebMediaPlayerWebRTC::OnPipelineError));
  } else {
    media_platform_api_ = media::MediaPlatformAPI::Create(
        media_task_runner_, client_->IsVideo(), app_id_,
        BIND_TO_RENDER_LOOP(&WebMediaPlayerWebRTC::OnVideoSizeChanged),
        BIND_TO_RENDER_LOOP(&WebMediaPlayerWebRTC::OnResumed),
        BIND_TO_RENDER_LOOP(&WebMediaPlayerWebRTC::OnSuspended),
        BIND_TO_RENDER_LOOP_VIDEO_FRAME_PROVIDER(
            &VideoFrameProviderImpl::ActiveRegionChanged),
        BIND_TO_RENDER_LOOP(&WebMediaPlayerWebRTC::OnPipelineError));
  }

  media_platform_api_->SetMediaPreferences(
      MediaPreferences::Get()->GetRawMediaPreferences());
  media_platform_api_->SetMediaCodecCapabilities(
      MediaPreferences::Get()->GetMediaCodecCapabilities());

  if (video_window_info_)
    media_platform_api_->SetMediaLayerId(video_window_info_->native_window_id);

  media_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&WebMediaPlayerWebRTC::InitMediaPlatformAPI,
                                weak_ptr_this_));
}

void WebMediaPlayerWebRTC::InitMediaPlatformAPI() {
  DCHECK(media_task_runner_->BelongsToCurrentThread());

  // audio data handling and rendering path is separate. We are leaving
  // it to be taken care by Chromium now. So, we dont need any audio config
  AudioDecoderConfig audio_config;
  VideoDecoderConfig video_config = GetVideoConfig();

  NEVA_LOGTF(INFO) << ": natural_size: "
                   << video_config.natural_size().ToString();

  media_platform_api_->Initialize(
      audio_config, video_config,
      base::Bind(&WebMediaPlayerWebRTC::OnMediaPlatformAPIInitialized,
                 weak_factory_this_.GetWeakPtr()));
}

void WebMediaPlayerWebRTC::ReleaseMediaPlatformAPI() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  NEVA_VLOGTF(1);

  is_destroying_ = true;

  {
    base::AutoLock auto_lock(frame_lock_);
    pending_encoded_frames_.clear();
  }

  compositor_->ReplaceCurrentFrameWithACopy();

  if (!media_platform_api_)
    return;

  handle_encoded_frames_ = false;
  media_platform_api_->Finalize();

  // Make sure to stop the pipeline so there's no more media threads running.
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  media_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&base::WaitableEvent::Signal, base::Unretained(&event)));
  event.Wait();

  media_platform_api_ = nullptr;

  pipeline_running_ = false;
  pipeline_status_ = media::PIPELINE_OK;
  has_first_frame_ = false;
}

void WebMediaPlayerWebRTC::OnPipelineFeed() {
  DCHECK(media_task_runner_->BelongsToCurrentThread());

  if (is_destroying_)
    return;

  std::vector<scoped_refptr<media::VideoFrame>> encoded_frames;
  {
    base::AutoLock auto_lock(frame_lock_);
    encoded_frames.swap(pending_encoded_frames_);
  }

  for (const auto& encoded_frame : encoded_frames) {
    scoped_refptr<media::DecoderBuffer> buffer = media::DecoderBuffer::CopyFrom(
        encoded_frame->data(0), encoded_frame->data_size(0));
    buffer->set_timestamp(encoded_frame->timestamp());
    buffer->set_is_key_frame(encoded_frame->metadata().key_frame == true);
    media_platform_api_->Feed(buffer, FeedType::kVideo);
  }
}

void WebMediaPlayerWebRTC::SuspendInternal() {
  NEVA_LOGTF(INFO) << ": delegate_id_: " << delegate_id_;

  if (is_suspended_)
    return;

  status_on_suspended_ = Paused() ? StatusOnSuspended::PausedStatus
                                  : StatusOnSuspended::PlayingStatus;
  if (media_platform_api_) {
    SuspendReason reason = client_->IsSuppressedMediaPlay()
                               ? SuspendReason::kBackgrounded
                               : SuspendReason::kSuspendedByPolicy;
    media_platform_api_->Suspend(reason);
  }

  is_suspended_ = true;
  has_activation_permit_ = false;

  // TODO(neva): also need to set STORAGE_BLACK for NEVA_VIDEO_HOLE ?
  if (HasVideo() && is_render_mode_texture())
    video_frame_provider_impl_->SetFrameType(VideoFrameProviderImpl::kBlack);

  // Usually we wait until OnSuspended(), but send DidMediaSuspended()
  // immediately when media_platform_api_ is null.
  if (!media_platform_api_)
    client_->DidMediaSuspended();
}

void WebMediaPlayerWebRTC::ResumeInternal() {
  NEVA_LOGTF(INFO) << ": delegate_id_: " << delegate_id_;

  if (!is_suspended_)
    return;

  is_suspended_ = false;

  media::RestorePlaybackMode restore_playback_mode =
      (status_on_suspended_ == StatusOnSuspended::PausedStatus)
          ? media::RestorePlaybackMode::kPaused
          : media::RestorePlaybackMode::kPlaying;

  if (media_platform_api_) {
    media_platform_api_->Resume(paused_time_, restore_playback_mode);
  } else {
    // Usually we wait until OnResumed(), but send DidMediaActivated()
    // immediately when media_platform_api_ is null.
    client_->DidMediaActivated();
  }
}

void WebMediaPlayerWebRTC::OnLoadPermitted() {
  // call base-class implementation
  if (!EnsureVideoWindowCreated()) {
    pending_load_media_ = true;
    return;
  }

  main_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&WebMediaPlayerWebRTC::ContinuePlayerWithWindowId,
                     weak_ptr_this_));
}

void WebMediaPlayerWebRTC::OnVideoSizeChanged(const gfx::Size& coded_size,
                                              const gfx::Size& natural_size) {
  NEVA_VLOGTF(1) << "natural_size: " << natural_size.ToString();

  coded_size_ = coded_size;
  natural_size_ = natural_size;
  if (video_window_remote_)
    video_window_remote_->SetVideoSize(coded_size_, natural_size_);
}

void WebMediaPlayerWebRTC::OnResumed() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (status_on_suspended_ == StatusOnSuspended::PausedStatus) {
    Pause();
    status_on_suspended_ = StatusOnSuspended::UnknownStatus;
  } else {
    Play();
    client_->ResumePlayback();
  }

  if (HasVideo() && is_render_mode_texture())
    video_frame_provider_impl_->SetFrameType(VideoFrameProviderImpl::kTexture);

  client_->DidMediaActivated();
}

void WebMediaPlayerWebRTC::OnSuspended() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  client_->DidMediaSuspended();
}

// It returns true if video window is already created and can be continued
// to next step.
bool WebMediaPlayerWebRTC::EnsureVideoWindowCreated() {
  NEVA_VLOGTF(1);

  if (video_window_info_)
    return true;

  // |is_bound()| would be true if we already requested so we need to just wait
  // for response
  if (video_window_client_receiver_.is_bound())
    return false;

  mojo::PendingRemote<ui::mojom::VideoWindowClient> pending_client;
  video_window_client_receiver_.Bind(
      pending_client.InitWithNewPipeAndPassReceiver());

  mojo::PendingRemote<ui::mojom::VideoWindow> pending_window_remote;
  create_video_window_cb_.Run(
      std::move(pending_client),
      pending_window_remote.InitWithNewPipeAndPassReceiver(),
      ui::VideoWindowParams());
  video_window_remote_.Bind(std::move(pending_window_remote));
  return false;
}

void WebMediaPlayerWebRTC::ContinuePlayerWithWindowId() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  NEVA_VLOGTF(1);

  if (pending_load_media_) {
    // call base-class implementation
    blink::WebMediaPlayerSource pending_source(pending_stream_);
    blink::WebMediaPlayerMS::Load(pending_load_type_, pending_source,
                                  pending_cors_mode_,
                                  pending_is_cache_disabled_);
    pending_load_media_ = false;
  }
}

void WebMediaPlayerWebRTC::OnMediaPlatformAPIInitialized(
    PipelineStatus status) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (is_destroying_ || !media_platform_api_) {
    NEVA_LOGTF(ERROR) << "Is destroying";
    return;
  }

  NEVA_VLOGTF(1) << "status : " << status;
  pipeline_running_ = true;
  pipeline_status_ = status;

  media_platform_api_->SetPlaybackRate(1.0f);

  scoped_refptr<media::VideoFrame> encoded_frame;
  {
    base::AutoLock auto_lock(frame_lock_);
    DCHECK(!pending_encoded_frames_.empty());
    encoded_frame = pending_encoded_frames_.back();
  }

  EnqueueHoleFrame(encoded_frame);

  media_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&WebMediaPlayerWebRTC::OnPipelineFeed, weak_ptr_this_));
}

void WebMediaPlayerWebRTC::OnPipelineError(PipelineStatus status) {
  NEVA_VLOGTF(1) << ": delegate_id_: " << delegate_id_
                 << " status : " << status;

  if (is_loading_) {
    is_loading_ = false;
    client_->DidMediaActivated();
  }

  if (is_destroying_)
    return;

  if ((status == DECODER_ERROR_RESOURCE_IS_RELEASED ||
       status == PIPELINE_ERROR_ABORT) &&
      !software_fallback_callback_.is_null())
    software_fallback_callback_.Run();

  {
    base::AutoLock auto_lock(frame_lock_);
    pending_encoded_frames_.clear();
  }

  compositor_->ReplaceCurrentFrameWithACopy();

  pipeline_running_ = false;
  pipeline_status_ = status;

  SetNetworkState(blink::PipelineErrorToNetworkState(status));

  RepaintInternal();
}

void WebMediaPlayerWebRTC::EnqueueHoleFrame(
    const scoped_refptr<media::VideoFrame>& input_frame) {
  if (frame_size_ == input_frame->natural_size())
    return;

  frame_size_ = input_frame->natural_size();

  scoped_refptr<media::VideoFrame> video_frame =
      media::VideoFrame::CreateTransparentFrame(frame_size_);

  if (video_frame.get()) {
    video_frame->set_timestamp(input_frame->timestamp());

    // Copy all metadata to the video frame.
    video_frame->metadata().MergeMetadataFrom(input_frame->metadata());

    // WebMediaPlayerMSCompositor::EnqueueFrame needs VideoFrame to continue
    // the webrtc video pipeline. So we pass hole frame to the same.
    blink::WebMediaPlayerMS::EnqueueHoleFrame(video_frame);

    RepaintInternal();
  }
}

VideoDecoderConfig WebMediaPlayerWebRTC::GetVideoConfig() {
  VideoCodecProfile profile = media::VIDEO_CODEC_PROFILE_UNKNOWN;
  switch (codec_) {
    case media::kCodecH264:
      profile = media::H264PROFILE_MIN;
      break;
    case media::kCodecVP8:
      profile = media::VP8PROFILE_ANY;
      break;
    case media::kCodecVP9:
      profile = media::VP9PROFILE_MIN;
      break;
    default:
      // forgot to handle new encoded video format?
      NOTREACHED();
      break;
  }
  NEVA_LOGTF(INFO) << ", codec: " << codec_
                   << ", name: " << media::GetCodecName(codec_);

  media::VideoDecoderConfig video_config(
      codec_, profile, media::VideoDecoderConfig::AlphaMode::kIsOpaque,
      media::VideoColorSpace(), media::kNoTransformation, kDefaultSize,
      gfx::Rect(kDefaultSize),
      // kDefaultSize, media::EmptyExtraData(), media::Unencrypted());
      kDefaultSize, media::EmptyExtraData(),
      media::EncryptionScheme::kUnencrypted);
  video_config.set_live_stream(true);
  return video_config;
}

}  // namespace media
