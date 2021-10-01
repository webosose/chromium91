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

#ifndef UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_IMPL_H_
#define UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_IMPL_H_

#include "base/memory/weak_ptr.h"
#include "base/unguessable_token.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/ozone/common/neva/video_window.h"
#include "ui/ozone/common/neva/video_window_controller.h"
#include "ui/ozone/common/neva/video_window_provider.h"
#include "ui/ozone/common/neva/video_window_provider_delegate.h"
#include "ui/platform_window/neva/mojom/video_window.mojom.h"
#include "ui/platform_window/neva/video_window_info.h"

namespace ui {

class VideoWindowMojo;

class VideoWindowProviderImpl : public VideoWindowProvider,
                                public VideoWindowProviderDelegate::Client {
 public:
  VideoWindowProviderImpl();
  VideoWindowProviderImpl(const VideoWindowProviderImpl&) = delete;
  VideoWindowProviderImpl& operator=(const VideoWindowProviderImpl&) = delete;

  ~VideoWindowProviderImpl() override;

  void SetVideoWindowController(VideoWindowController* video_window_controller);

  void SetDelegate(VideoWindowProviderDelegate* delegate);

  // Implements VideoWindowProviderDelegate
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

  void OwnerWidgetStateChanged(gfx::AcceleratedWidget widget,
                               WidgetState state) override;

  void OwnerWidgetClosed(gfx::AcceleratedWidget widget) override;

  // Implements VideoWindowProviderDelegate::Client
  // Ownership of |video_window| should be held by
  // VideoWindowProviderDelegate.
  void OnVideoWindowCreated(bool success,
                            const base::UnguessableToken& window_id,
                            ui::VideoWindow* video_window) override;

  void OnVideoWindowDestroyed(const base::UnguessableToken& window_id) override;

 private:
  std::map<base::UnguessableToken, std::unique_ptr<VideoWindowMojo>>
      video_windows_;
  VideoWindowController* video_window_controller_ = nullptr;
  VideoWindowProviderDelegate* delegate_ = nullptr;
  const scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  base::WeakPtr<VideoWindowProviderImpl> weak_this_;
  base::WeakPtrFactory<VideoWindowProviderImpl> weak_factory_;
};

}  // namespace ui

#endif  // UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_PROVIDER_IMPL_H_
