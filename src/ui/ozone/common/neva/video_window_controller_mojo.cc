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

#include "ui/ozone/common/neva/video_window_controller_mojo.h"

namespace ui {

VideoWindowControllerMojo::VideoWindowControllerMojo(
    ui::VideoWindowProvider* provider,
    mojo::Remote<mojom::VideoWindowProviderClient> video_window_controller)
    : provider_(provider) {
  video_window_controller_ = std::move(video_window_controller);

  mojo::PendingRemote<mojom::VideoWindowProvider> pending_client;
  receiver_.Bind(pending_client.InitWithNewPipeAndPassReceiver());

  if (video_window_controller_) {
    video_window_controller_->RegisterVideoWindowProvider(
        std::move(pending_client));
  }
}

VideoWindowControllerMojo::~VideoWindowControllerMojo() {}

void VideoWindowControllerMojo::OnVideoWindowCreated(
    const base::UnguessableToken& window_id,
    bool success) {
  if (video_window_controller_)
    video_window_controller_->OnVideoWindowCreated(window_id, success);
}

void VideoWindowControllerMojo::OnVideoWindowDestroyed(
    const base::UnguessableToken& window_id) {
  if (video_window_controller_)
    video_window_controller_->OnVideoWindowDestroyed(window_id);
}

void VideoWindowControllerMojo::CreateVideoWindow(
    gfx::AcceleratedWidget widget,
    const base::UnguessableToken& window_id,
    mojo::PendingRemote<mojom::VideoWindowClient> client,
    mojo::PendingReceiver<mojom::VideoWindow> receiver,
    const VideoWindowParams& params) {
  if (provider_) {
    provider_->CreateVideoWindow(widget, window_id, std::move(client),
                                 std::move(receiver), params);
  }
}

void VideoWindowControllerMojo::DestroyVideoWindow(
    const base::UnguessableToken& window_id) {
  if (provider_)
    provider_->DestroyVideoWindow(window_id);
}

void VideoWindowControllerMojo::VideoWindowGeometryChanged(
    const base::UnguessableToken& window_id,
    const gfx::Rect& dest_rect) {
  if (provider_)
    provider_->VideoWindowGeometryChanged(window_id, dest_rect);
}

void VideoWindowControllerMojo::VideoWindowVisibilityChanged(
    const base::UnguessableToken& window_id,
    bool visibility) {
  if (provider_)
    provider_->VideoWindowVisibilityChanged(window_id, visibility);
}

}  // namespace ui
