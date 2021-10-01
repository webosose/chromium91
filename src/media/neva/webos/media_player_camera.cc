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

#include "media/neva/webos/media_player_camera.h"

#include <algorithm>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "media/base/bind_to_current_loop.h"
#include "neva/logging.h"
#include "third_party/jsoncpp/source/include/json/json.h"

namespace media {

MediaPlayerCamera::MediaPlayerCamera(
    MediaPlayerNevaClient* client,
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
    const std::string& app_id)
    : client_(client), app_id_(app_id), main_task_runner_(main_task_runner) {
  NEVA_VLOGTF(1);
  umedia_client_ =
      WebOSMediaClient::Create(main_task_runner_, AsWeakPtr(), app_id_);
}

MediaPlayerCamera::~MediaPlayerCamera() {
  NEVA_VLOGTF(1);
  DCHECK(main_task_runner_->BelongsToCurrentThread());
}

void MediaPlayerCamera::Initialize(const bool is_video,
                                   const double current_time,
                                   const std::string& url,
                                   const std::string& mime_type,
                                   const std::string& referrer,
                                   const std::string& user_agent,
                                   const std::string& cookies,
                                   const std::string& media_option,
                                   const std::string& custom_option) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(2) << "app_id: " << app_id_ << " url : " << url
                  << " custom_option - "
                  << (custom_option.empty() ? "{}" : custom_option);

  url::Parsed parsed;
  url::ParseStandardURL(url.c_str(), url.length(), &parsed);
  url_ = GURL(url, parsed, true);
  mime_type_ = mime_type;

  umedia_client_->Load(is_video, current_time, false, url, mime_type, referrer,
                       user_agent, cookies, custom_option);
}

void MediaPlayerCamera::Start() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(2);

  umedia_client_->SetPlaybackRate(playback_rate_);
}

void MediaPlayerCamera::Pause() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(2);
}

bool MediaPlayerCamera::IsPreloadable(const std::string& content_media_option) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  return false;
}

bool MediaPlayerCamera::HasVideo() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return umedia_client_->HasVideo();
}

bool MediaPlayerCamera::HasAudio() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return umedia_client_->HasAudio();
}

bool MediaPlayerCamera::SelectTrack(const MediaTrackType type,
                                    const std::string& id) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return true;
}

bool MediaPlayerCamera::UsesIntrinsicSize() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return umedia_client_->UsesIntrinsicSize();
}

std::string MediaPlayerCamera::MediaId() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return umedia_client_->MediaId();
}

bool MediaPlayerCamera::RequireMediaResource() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return true;
}

void MediaPlayerCamera::OnPlaybackStateChanged(bool playing) {
  NEVA_DVLOGTF(1);

  if (!client_)
    return;

  Json::Value root;
  root["mediaId"] = MediaId().c_str();
  root["infoType"] = "cameraState";

  if (playing) {
    client_->OnMediaPlayerPlay();
    root["cameraState"] = "playing";
  } else {
    client_->OnMediaPlayerPause();
    root["cameraState"] = "paused";
  }

  Json::FastWriter writer;
  client_->OnCustomMessage(media::MediaEventType::kMediaEventUpdateCameraState,
                           writer.write(root));
}

void MediaPlayerCamera::OnPlaybackEnded() {
  NEVA_DVLOGTF(1);
}

void MediaPlayerCamera::OnBufferingStatusChanged(
    WebOSMediaClient::BufferingState buffering_state) {
  switch (buffering_state) {
    case WebOSMediaClient::BufferingState::kHaveMetadata: {
      umedia_client_->SetPlaybackRate(playback_rate_);
      gfx::Size coded_size = umedia_client_->GetCodedVideoSize();
      gfx::Size natural_size = umedia_client_->GetNaturalVideoSize();
      client_->OnMediaMetadataChanged(
          base::TimeDelta::FromSecondsD(umedia_client_->GetDuration()),
          coded_size, natural_size, true);
      break;
    }
    case WebOSMediaClient::BufferingState::kLoadCompleted:
    case WebOSMediaClient::BufferingState::kPreloadCompleted:
      client_->OnLoadComplete();
      break;
    case WebOSMediaClient::BufferingState::kPrerollCompleted:
    case WebOSMediaClient::BufferingState::kWebOSBufferingStart:
    case WebOSMediaClient::BufferingState::kWebOSBufferingEnd:
    case WebOSMediaClient::BufferingState::kWebOSNetworkStateLoading:
    case WebOSMediaClient::BufferingState::kWebOSNetworkStateLoaded:
    default:
      break;
  }
}

void MediaPlayerCamera::OnVideoSizeChanged() {
  NEVA_DVLOGTF(1);
  client_->OnVideoSizeChanged(umedia_client_->GetCodedVideoSize(),
                              umedia_client_->GetNaturalVideoSize());
}

void MediaPlayerCamera::OnUMSInfoUpdated(const std::string& detail) {
  NEVA_DVLOGTF(1);

  if (!client_ || detail.empty())
    return;

  Json::Reader reader;
  Json::Value root;
  if (!reader.parse(detail, root)) {
    NEVA_LOGTF(ERROR) << "Json::Reader::parse (" << detail << ") - Failed!!!";
    return;
  }

  if (root.isMember("cameraId"))
    camera_id_ = root["cameraId"].asString();

  client_->OnCustomMessage(media::MediaEventType::kMediaEventUpdateCameraState,
                           detail);
}

void MediaPlayerCamera::OnEncryptedMediaInitData(
    const std::string& init_data_type,
    const std::vector<uint8_t>& init_data) {
  NOTIMPLEMENTED();
}

void MediaPlayerCamera::OnTimeUpdated(base::TimeDelta current_time) {
  client_->OnTimeUpdate(current_time, base::TimeTicks::Now());
}

}  // namespace media
