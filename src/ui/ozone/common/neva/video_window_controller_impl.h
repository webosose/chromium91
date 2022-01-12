// Copyright 2019 LG Electronics, Inc.
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

#ifndef UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_CONTROLLER_IMPL_H_
#define UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_CONTROLLER_IMPL_H_

#include <map>
#include <set>

#include "base/unguessable_token.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/ozone/common/neva/video_window_controller.h"
#include "ui/ozone/common/neva/video_window_provider.h"
#include "ui/platform_window/neva/mojom/video_window.mojom.h"
#include "ui/platform_window/neva/video_window_geometry_manager.h"

namespace ui {
/* VideoWindowControllerImpl lives in gpu process and it request
 * creating/destroying/geomtry-update VideoWindow to VideoWindowProvider */
class VideoWindowControllerImpl : public mojom::VideoWindowConnector,
                                  public VideoWindowController,
                                  public VideoWindowGeometryManager {
 public:
  VideoWindowControllerImpl();
  VideoWindowControllerImpl(const VideoWindowControllerImpl&) = delete;
  VideoWindowControllerImpl& operator=(const VideoWindowControllerImpl&) =
      delete;
  ~VideoWindowControllerImpl() override;

  void Initialize(
      const scoped_refptr<base::SingleThreadTaskRunner>& task_runner);

  void SetVideoWindowProvider(VideoWindowProvider* provider);

  // Bind the manager to a mojo interface receiver.
  void Bind(mojo::PendingReceiver<mojom::VideoWindowConnector> receiver);

  // Implements VideoWindowController
  void OnVideoWindowCreated(const base::UnguessableToken& window_id,
                            bool success) override;

  void OnVideoWindowDestroyed(const base::UnguessableToken& window_id) override;

  // Implements mojom::VideoWindowConnector
  void CreateVideoWindow(gfx::AcceleratedWidget widget,
                         mojo::PendingRemote<mojom::VideoWindowClient> client,
                         mojo::PendingReceiver<mojom::VideoWindow> receiver,
                         const VideoWindowParams& param) override;

  // Implements VideoWindowGeometryManager
  bool IsInitialized() override;
  void NotifyVideoWindowGeometryChanged(gfx::AcceleratedWidget widget,
                                        const base::UnguessableToken& window_id,
                                        const gfx::Rect& rect) override;
  void BeginOverlayProcessor(gfx::AcceleratedWidget widget) override;
  void EndOverlayProcessor(gfx::AcceleratedWidget widget) override;

 private:
  class VideoWindowInfo;
  VideoWindowInfo* FindVideoWindowInfo(const base::UnguessableToken& window_id);
  void RemoveVideoWindowInfo(const base::UnguessableToken& window_id);
  void SetVideoWindowVisibility(const base::UnguessableToken& window_id,
                                bool visibility);

  using VideoWindowInfoList = std::vector<std::unique_ptr<VideoWindowInfo>>;

  VideoWindowProvider* provider_ = nullptr;

  std::map<base::UnguessableToken, gfx::AcceleratedWidget> id_to_widget_map_;
  std::map<gfx::AcceleratedWidget, VideoWindowInfoList> video_windows_;
  std::map<gfx::AcceleratedWidget, std::set<base::UnguessableToken>>
      hidden_candidate_;

  mojo::ReceiverSet<ui::mojom::VideoWindowConnector> receivers_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  bool initialized_ = false;
};

}  // namespace ui

#endif  // UI_OZONE_COMMON_NEVA_VIDEO_WINDOW_CONTROLLER_IMPL_H_
