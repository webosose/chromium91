// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/public/cpp/ambient/fake_ambient_backend_controller_impl.h"

#include <array>
#include <utility>

#include "ash/public/cpp/ambient/ambient_backend_controller.h"
#include "ash/public/cpp/ambient/common/ambient_settings.h"
#include "base/callback.h"
#include "base/optional.h"
#include "base/threading/sequenced_task_runner_handle.h"

namespace ash {

namespace {

constexpr AmbientModeTopicSource kTopicSource =
    AmbientModeTopicSource::kGooglePhotos;

constexpr AmbientModeTemperatureUnit kTemperatureUnit =
    AmbientModeTemperatureUnit::kCelsius;

constexpr char kFakeUrl[] = "chrome://ambient";

constexpr char kFakeDetails[] = "fake-photo-attribution";

constexpr std::array<const char*, 2> kFakeBackupPhotoUrls = {kFakeUrl,
                                                             kFakeUrl};

AmbientSettings CreateFakeSettings() {
  AmbientSettings settings;
  settings.topic_source = kTopicSource;
  settings.temperature_unit = kTemperatureUnit;

  ArtSetting art_setting0;
  art_setting0.album_id = "0";
  art_setting0.enabled = true;
  art_setting0.title = "art0";
  art_setting0.visible = true;
  settings.art_settings.emplace_back(art_setting0);

  ArtSetting art_setting1;
  art_setting1.album_id = "1";
  art_setting1.enabled = false;
  art_setting1.title = "art1";
  art_setting1.visible = true;
  settings.art_settings.emplace_back(art_setting1);

  ArtSetting hidden_setting;
  hidden_setting.album_id = "2";
  hidden_setting.enabled = false;
  hidden_setting.title = "hidden";
  hidden_setting.visible = false;
  settings.art_settings.emplace_back(hidden_setting);

  settings.selected_album_ids = {"1"};
  return settings;
}

PersonalAlbums CreateFakeAlbums() {
  PersonalAlbums albums;
  PersonalAlbum album0;
  album0.album_id = "0";
  album0.album_name = "album0";
  albums.albums.emplace_back(std::move(album0));

  PersonalAlbum album1;
  album1.album_id = "1";
  album1.album_name = "album1";
  albums.albums.emplace_back(std::move(album1));

  return albums;
}

}  // namespace

FakeAmbientBackendControllerImpl::FakeAmbientBackendControllerImpl() = default;
FakeAmbientBackendControllerImpl::~FakeAmbientBackendControllerImpl() = default;

void FakeAmbientBackendControllerImpl::FetchScreenUpdateInfo(
    int num_topics,
    OnScreenUpdateInfoFetchedCallback callback) {
  ash::ScreenUpdate update;

  for (int i = 0; i < num_topics; i++) {
    ash::AmbientModeTopic topic;
    topic.url = kFakeUrl;
    topic.details = kFakeDetails;
    topic.related_image_url = kFakeUrl;
    topic.topic_type = AmbientModeTopicType::kCulturalInstitute;

    update.next_topics.emplace_back(topic);
  }

  // Only respond weather info when there is no active weather testing.
  if (!weather_info_) {
    ash::WeatherInfo weather_info;
    weather_info.temp_f = .0f;
    weather_info.condition_icon_url = kFakeUrl;
    weather_info.show_celsius = true;
    update.weather_info = weather_info;
  }

  // Pretend to respond asynchronously.
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), update));
}

void FakeAmbientBackendControllerImpl::GetSettings(
    GetSettingsCallback callback) {
  // Pretend to respond asynchronously.
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), CreateFakeSettings()));
}

void FakeAmbientBackendControllerImpl::UpdateSettings(
    const AmbientSettings& settings,
    UpdateSettingsCallback callback) {
  // |show_weather| should always be set to true.
  DCHECK(settings.show_weather);
  pending_update_callback_ = std::move(callback);
}

void FakeAmbientBackendControllerImpl::FetchSettingPreview(
    int preview_width,
    int preview_height,
    OnSettingPreviewFetchedCallback callback) {
  std::vector<std::string> urls = {kFakeUrl};
  // Pretend to respond asynchronously.
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), urls));
}

void FakeAmbientBackendControllerImpl::FetchPersonalAlbums(
    int banner_width,
    int banner_height,
    int num_albums,
    const std::string& resume_token,
    OnPersonalAlbumsFetchedCallback callback) {
  // Pretend to respond asynchronously.
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), CreateFakeAlbums()));
}

void FakeAmbientBackendControllerImpl::FetchSettingsAndAlbums(
    int banner_width,
    int banner_height,
    int num_albums,
    OnSettingsAndAlbumsFetchedCallback callback) {
  pending_fetch_settings_albums_callback_ = std::move(callback);
}

void FakeAmbientBackendControllerImpl::FetchWeather(
    FetchWeatherCallback callback) {
  std::move(callback).Run(weather_info_);
}

const std::array<const char*, 2>&
FakeAmbientBackendControllerImpl::GetBackupPhotoUrls() const {
  return kFakeBackupPhotoUrls;
}

void FakeAmbientBackendControllerImpl::ReplyFetchSettingsAndAlbums(
    bool success,
    const base::Optional<AmbientSettings>& settings) {
  if (!pending_fetch_settings_albums_callback_)
    return;

  if (success) {
    std::move(pending_fetch_settings_albums_callback_)
        .Run(settings.value_or(CreateFakeSettings()), CreateFakeAlbums());
  } else {
    std::move(pending_fetch_settings_albums_callback_)
        .Run(/*settings=*/base::nullopt, PersonalAlbums());
  }
}

bool FakeAmbientBackendControllerImpl::IsFetchSettingsAndAlbumsPending() const {
  return !pending_fetch_settings_albums_callback_.is_null();
}

void FakeAmbientBackendControllerImpl::ReplyUpdateSettings(bool success) {
  if (!pending_update_callback_)
    return;

  std::move(pending_update_callback_).Run(success);
}

bool FakeAmbientBackendControllerImpl::IsUpdateSettingsPending() const {
  return !pending_update_callback_.is_null();
}

void FakeAmbientBackendControllerImpl::SetWeatherInfo(
    base::Optional<WeatherInfo> info) {
  weather_info_ = std::move(info);
}

}  // namespace ash
