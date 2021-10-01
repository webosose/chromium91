// Copyright 2021 LG Electronics, Inc.
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

#include "ui/ozone/common/neva/video_window_provider_mojo.h"

namespace ui {

VideoWindowProviderMojo::VideoWindowProviderMojo(
    VideoWindowController* controller,
    mojo::PendingReceiver<mojom::VideoWindowProviderClient> receiver)
    : controller_(controller) {
  receiver_.Bind(std::move(receiver));
}

VideoWindowProviderMojo::~VideoWindowProviderMojo() {}

void VideoWindowProviderMojo::CreateVideoWindow(
    gfx::AcceleratedWidget widget,
    const base::UnguessableToken& window_id,
    mojo::PendingRemote<mojom::VideoWindowClient> client,
    mojo::PendingReceiver<mojom::VideoWindow> receiver,
    const VideoWindowParams& params) {
  if (video_window_provider_) {
    video_window_provider_->CreateVideoWindow(
        widget, window_id, std::move(client), std::move(receiver), params);
  }
}

void VideoWindowProviderMojo::DestroyVideoWindow(
    const base::UnguessableToken& window_id) {
  if (video_window_provider_)
    video_window_provider_->DestroyVideoWindow(window_id);
}

void VideoWindowProviderMojo::VideoWindowGeometryChanged(
    const base::UnguessableToken& window_id,
    const gfx::Rect& dest_rect) {
  if (video_window_provider_)
    video_window_provider_->VideoWindowGeometryChanged(window_id, dest_rect);
}

void VideoWindowProviderMojo::VideoWindowVisibilityChanged(
    const base::UnguessableToken& window_id,
    bool visibility) {
  if (video_window_provider_)
    video_window_provider_->VideoWindowVisibilityChanged(window_id, visibility);
}

void VideoWindowProviderMojo::RegisterVideoWindowProvider(
    mojo::PendingRemote<mojom::VideoWindowProvider> provider) {
  mojo::Remote<mojom::VideoWindowProvider> video_window_provider(
      std::move(provider));
  video_window_provider_ = std::move(video_window_provider);
}

void VideoWindowProviderMojo::OnVideoWindowCreated(
    const base::UnguessableToken& window_id,
    bool success) {
  if (controller_)
    controller_->OnVideoWindowCreated(window_id, success);
}

void VideoWindowProviderMojo::OnVideoWindowDestroyed(
    const base::UnguessableToken& window_id) {
  if (controller_)
    controller_->OnVideoWindowDestroyed(window_id);
}

}  // namespace ui
