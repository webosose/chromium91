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

#ifndef UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_MOJO_H_
#define UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_MOJO_H_

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/ozone/common/neva/mojom/video_window_provider.mojom.h"
#include "ui/ozone/common/neva/video_window_controller_impl.h"
#include "ui/ozone/common/neva/video_window_provider.h"

namespace ui {

class VideoWindowProviderMojo : public VideoWindowProvider,
                                public mojom::VideoWindowProviderClient {
 public:
  explicit VideoWindowProviderMojo(
      VideoWindowController* controller,
      mojo::PendingReceiver<mojom::VideoWindowProviderClient> receiver);
  VideoWindowProviderMojo(const VideoWindowProviderMojo&) = delete;
  VideoWindowProviderMojo& operator=(const VideoWindowProviderMojo&) = delete;

  ~VideoWindowProviderMojo() override;

  // Implements VideoWindowProvider
  void CreateVideoWindow(gfx::AcceleratedWidget widget,
                         const base::UnguessableToken& window_id,
                         mojo::PendingRemote<mojom::VideoWindowClient> client,
                         mojo::PendingReceiver<mojom::VideoWindow> receiver,
                         const VideoWindowParams& params) override;

  void DestroyVideoWindow(const base::UnguessableToken& window_id) override;

  void VideoWindowGeometryChanged(const base::UnguessableToken& window_id,
                                  const gfx::Rect& dest_rect) override;

  void VideoWindowVisibilityChanged(const base::UnguessableToken& window_id,
                                    bool visibility) override;

  // Implements mojom::VideoWindowProviderClient
  void RegisterVideoWindowProvider(
      mojo::PendingRemote<mojom::VideoWindowProvider> provider) override;

  void OnVideoWindowCreated(const base::UnguessableToken& window_id,
                            bool success) override;

  void OnVideoWindowDestroyed(const base::UnguessableToken& window_id) override;

 private:
  VideoWindowController* controller_ = nullptr;
  mojo::Remote<mojom::VideoWindowProvider> video_window_provider_;
  mojo::Receiver<mojom::VideoWindowProviderClient> receiver_{this};
};

}  // namespace ui

#endif  // UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_MOJO_H_
