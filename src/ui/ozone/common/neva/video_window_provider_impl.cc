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

#include "ui/ozone/common/neva/video_window_provider_impl.h"

#include "base/cancelable_callback.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"

namespace ui {

namespace {
const int kMinVideoGeometryUpdateIntervalMs = 200;

const char* WidgetStateToString(WidgetState state) {
#define STRINGIFY_owner_widget_STATE_CASE(state) \
  case WidgetState::state:                       \
    return #state

  switch (state) {
    STRINGIFY_owner_widget_STATE_CASE(UNINITIALIZED);
    STRINGIFY_owner_widget_STATE_CASE(SHOW);
    STRINGIFY_owner_widget_STATE_CASE(HIDE);
    STRINGIFY_owner_widget_STATE_CASE(FULLSCREEN);
    STRINGIFY_owner_widget_STATE_CASE(MAXIMIZED);
    STRINGIFY_owner_widget_STATE_CASE(MINIMIZED);
    STRINGIFY_owner_widget_STATE_CASE(RESTORE);
    STRINGIFY_owner_widget_STATE_CASE(ACTIVE);
    STRINGIFY_owner_widget_STATE_CASE(INACTIVE);
    STRINGIFY_owner_widget_STATE_CASE(RESIZE);
    STRINGIFY_owner_widget_STATE_CASE(DESTROYED);
  }
  return "null";
}
}  // namespace

class VideoWindowMojo : public mojom::VideoWindow {
 public:
  explicit VideoWindowMojo(VideoWindowProviderImpl* provider,
                           const base::UnguessableToken& window_id,
                           const VideoWindowParams& params,
                           mojo::PendingRemote<mojom::VideoWindowClient> client,
                           mojo::PendingReceiver<mojom::VideoWindow> receiver);
  VideoWindowMojo(const VideoWindowMojo&) = delete;
  VideoWindowMojo& operator=(const VideoWindowMojo&) = delete;

  ~VideoWindowMojo() override;

  void OnDisconnected();

  void SetVideoWindow(ui::VideoWindow* video_window);

  void SetVisibility(bool visibility);

  gfx::AcceleratedWidget GetOwnerWidget();

  void OnOwnerWidgetStateChanged(WidgetState state);

  // All |UpdateVideoWindow*| methods may no-op if there is no actual update.
  void UpdateVideoWindowGeometry(const gfx::Rect& dst);

  void CommitVideoWindowGeometry();

  void CommitVideoWindowGeometryIfNeeded(const gfx::Rect& src,
                                         const gfx::Rect& dst,
                                         const base::Optional<gfx::Rect>& ori);

  // Implements mojom::VideoWindow
  void SetVideoSize(const gfx::Size& coded_size,
                    const gfx::Size& natural_size) override;
  void SetProperty(const std::string& name, const std::string& value) override;
  void UpdateVideoWindowGeometry(const gfx::Rect& src,
                                 const gfx::Rect& dst) override;
  void UpdateVideoWindowGeometryWithCrop(const gfx::Rect& ori,
                                         const gfx::Rect& src,
                                         const gfx::Rect& dst) override;

 private:
  VideoWindowProviderImpl* provider_ = nullptr;
  base::UnguessableToken window_id_;
  ui::VideoWindow* video_window_ = nullptr;
  VideoWindowParams params_;

  gfx::Rect src_rect_;
  gfx::Rect dst_rect_;
  base::Optional<gfx::Rect> ori_rect_ = base::nullopt;
  base::Optional<gfx::Size> coded_size_;
  base::Optional<gfx::Size> natural_size_;
  base::Time last_updated_ = base::Time::Now();
  base::CancelableOnceCallback<void()> commit_geometry_cb_;

  bool owner_widget_shown_ = true;
  bool visible_in_screen_ = true;

  mojo::Remote<mojom::VideoWindowClient> client_;
  mojo::Receiver<mojom::VideoWindow> receiver_{this};
  mojo::PendingReceiver<mojom::VideoWindow> pending_receiver_;

  base::WeakPtr<VideoWindowMojo> weak_this_;
  base::WeakPtrFactory<VideoWindowMojo> weak_factory_;
};

VideoWindowMojo::VideoWindowMojo(
    VideoWindowProviderImpl* provider,
    const base::UnguessableToken& window_id,
    const VideoWindowParams& params,
    mojo::PendingRemote<mojom::VideoWindowClient> client,
    mojo::PendingReceiver<mojom::VideoWindow> receiver)
    : provider_(provider),
      window_id_(window_id),
      params_(params),
      client_(std::move(client)),
      pending_receiver_(std::move(receiver)),
      weak_factory_(this) {
  weak_this_ = weak_factory_.GetWeakPtr();
  VLOG(1) << __func__ << " window_id=" << window_id_;

  // To detect when the user stop using the window.
  client_.set_disconnect_handler(
      base::BindOnce(&VideoWindowMojo::OnDisconnected, weak_this_));
}

VideoWindowMojo::~VideoWindowMojo() {
  VLOG(1) << __func__;
  if (client_)
    client_->OnVideoWindowDestroyed();
}

void VideoWindowMojo::OnDisconnected() {
  if (provider_)
    provider_->DestroyVideoWindow(window_id_);
}

void VideoWindowMojo::SetVideoWindow(ui::VideoWindow* video_window) {
  video_window_ = video_window;

  if (video_window_) {
    receiver_.Bind(std::move(pending_receiver_));
    client_->OnVideoWindowCreated(
        {window_id_, video_window_->GetNativeWindowId()});
  }
}

void VideoWindowMojo::SetVisibility(bool visibility) {
  VLOG(1) << __func__ << " window_id_=" << window_id_
          << " visibility=" << visibility;

  if (video_window_)
    video_window_->SetVisibility(visibility);

  if (!commit_geometry_cb_.IsCancelled())
    CommitVideoWindowGeometry();
}

gfx::AcceleratedWidget VideoWindowMojo::GetOwnerWidget() {
  if (!video_window_)
    return gfx::kNullAcceleratedWidget;
  return video_window_->GetOwnerWidget();
}

void VideoWindowMojo::OnOwnerWidgetStateChanged(WidgetState state) {
  if (!video_window_)
    return;

  bool new_value = owner_widget_shown_;
  switch (state) {
    case WidgetState::MINIMIZED:
      new_value = false;
      break;
    case WidgetState::MAXIMIZED:
    case WidgetState::FULLSCREEN:
      new_value = true;
      break;
    default:
      break;
  }

  if (owner_widget_shown_ == new_value)
    return;

  owner_widget_shown_ = new_value;

  // No need to change video mute state for already muted video.
  if (params_.use_video_mute_on_app_minimized && visible_in_screen_)
    video_window_->SetVisibility(owner_widget_shown_);
}

void VideoWindowMojo::UpdateVideoWindowGeometry(const gfx::Rect& dst) {
  CommitVideoWindowGeometryIfNeeded(src_rect_, dst, base::nullopt);
}

void VideoWindowMojo::CommitVideoWindowGeometry() {
  if (!video_window_)
    return;

  if (!commit_geometry_cb_.IsCancelled())
    commit_geometry_cb_.Cancel();

  base::Optional<gfx::Size> video_size;
  if (params_.use_coded_size_for_original_rect)
    video_size = coded_size_;
  else
    video_size = natural_size_;

  video_window_->UpdateGeometry(src_rect_, dst_rect_, ori_rect_, video_size);
}

void VideoWindowMojo::CommitVideoWindowGeometryIfNeeded(
    const gfx::Rect& src,
    const gfx::Rect& dst,
    const base::Optional<gfx::Rect>& ori) {
  bool changed = false;

  if (ori_rect_ != ori) {
    ori_rect_ = ori;
    changed = true;
  }

  if (src_rect_ != src) {
    src_rect_ = src;
    changed = true;
  }

  if (dst_rect_ != dst) {
    dst_rect_ = dst;
    changed = true;
  }

  // If any geometry is not changed there is no reason to update.
  // Also if the callback is already scheduled, just wait for callback
  if (!changed || !commit_geometry_cb_.IsCancelled())
    return;

  const base::TimeDelta elapsed = base::Time::Now() - last_updated_;
  const base::TimeDelta interval =
      base::TimeDelta::FromMilliseconds(kMinVideoGeometryUpdateIntervalMs);
  if (elapsed < interval) {
    const base::TimeDelta next_update = interval - elapsed;
    commit_geometry_cb_.Reset(base::BindOnce(
        &VideoWindowMojo::CommitVideoWindowGeometry, weak_this_));
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, commit_geometry_cb_.callback(), next_update);
    return;
  }
  CommitVideoWindowGeometry();
}

void VideoWindowMojo::SetVideoSize(const gfx::Size& coded_size,
                                   const gfx::Size& natural_size) {
  coded_size_ = coded_size;
  natural_size_ = natural_size;
}

void VideoWindowMojo::SetProperty(const std::string& name,
                                  const std::string& value) {
  if (video_window_)
    video_window_->SetProperty(name, value);
}

void VideoWindowMojo::UpdateVideoWindowGeometry(const gfx::Rect& src,
                                                const gfx::Rect& dst) {
  VLOG(1) << __func__ << " src=" << src.ToString() << " dst=" << dst.ToString();

  CommitVideoWindowGeometryIfNeeded(src, dst, base::nullopt);
}

void VideoWindowMojo::UpdateVideoWindowGeometryWithCrop(const gfx::Rect& ori,
                                                        const gfx::Rect& src,
                                                        const gfx::Rect& dst) {
  VLOG(1) << __func__ << " ori=" << ori.ToString() << " src=" << src.ToString()
          << " dst=" << dst.ToString();

  CommitVideoWindowGeometryIfNeeded(src, dst, ori);
}

VideoWindowProviderImpl::VideoWindowProviderImpl()
    : task_runner_(base::ThreadTaskRunnerHandle::Get()), weak_factory_(this) {
  weak_this_ = weak_factory_.GetWeakPtr();
}

VideoWindowProviderImpl::~VideoWindowProviderImpl() {}

void VideoWindowProviderImpl::SetVideoWindowController(
    VideoWindowController* video_window_controller) {
  video_window_controller_ = video_window_controller;
}

void VideoWindowProviderImpl::SetDelegate(
    VideoWindowProviderDelegate* delegate) {
  delegate_ = delegate;
  if (delegate_)
    delegate_->SetClient(this);
}

void VideoWindowProviderImpl::CreateVideoWindow(
    gfx::AcceleratedWidget widget,
    const base::UnguessableToken& window_id,
    mojo::PendingRemote<mojom::VideoWindowClient> client,
    mojo::PendingReceiver<mojom::VideoWindow> receiver,
    const VideoWindowParams& params) {
  if (task_runner_ && !task_runner_->BelongsToCurrentThread()) {
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&VideoWindowProviderImpl::CreateVideoWindow, weak_this_,
                       widget, window_id, std::move(client),
                       std::move(receiver), params));
    return;
  }

  video_windows_[window_id] = std::make_unique<VideoWindowMojo>(
      this, window_id, params, std::move(client), std::move(receiver));

  if (delegate_)
    delegate_->CreateVideoWindow(widget, window_id);
}

void VideoWindowProviderImpl::DestroyVideoWindow(
    const base::UnguessableToken& window_id) {
  if (task_runner_ && !task_runner_->BelongsToCurrentThread()) {
    task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&VideoWindowProviderImpl::DestroyVideoWindow,
                                  weak_this_, window_id));
    return;
  }

  // Need to prevent accessing to ui::VideoWindow during destruction.
  auto video_window_iter = video_windows_.find(window_id);
  if (video_window_iter != video_windows_.end())
    video_window_iter->second->SetVideoWindow(nullptr);

  if (delegate_)
    delegate_->DestroyVideoWindow(window_id);
}

void VideoWindowProviderImpl::VideoWindowGeometryChanged(
    const base::UnguessableToken& window_id,
    const gfx::Rect& dest_rect) {
  if (task_runner_ && !task_runner_->BelongsToCurrentThread()) {
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&VideoWindowProviderImpl::VideoWindowGeometryChanged,
                       weak_this_, window_id, dest_rect));
    return;
  }

  auto video_window_iter = video_windows_.find(window_id);
  if (video_window_iter == video_windows_.end()) {
    LOG(ERROR) << __func__
               << " Cannot update video window geometry. window_id: "
               << window_id << " / dest_rect: " << dest_rect.ToString();
    return;
  }

  video_window_iter->second->UpdateVideoWindowGeometry(dest_rect);
}

void VideoWindowProviderImpl::VideoWindowVisibilityChanged(
    const base::UnguessableToken& window_id,
    bool visibility) {
  if (task_runner_ && !task_runner_->BelongsToCurrentThread()) {
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&VideoWindowProviderImpl::VideoWindowVisibilityChanged,
                       weak_this_, window_id, visibility));
    return;
  }

  auto video_window_iter = video_windows_.find(window_id);
  if (video_window_iter == video_windows_.end()) {
    LOG(ERROR) << __func__
               << " Cannot update video window visibility. window_id: "
               << window_id << " / visibility: " << visibility;
    return;
  }

  video_window_iter->second->SetVisibility(visibility);
}

void VideoWindowProviderImpl::OwnerWidgetStateChanged(
    gfx::AcceleratedWidget widget,
    WidgetState state) {
  if (task_runner_ && !task_runner_->BelongsToCurrentThread()) {
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&VideoWindowProviderImpl::OwnerWidgetStateChanged,
                       weak_this_, widget, state));
    return;
  }
  VLOG(1) << __func__ << " widget=" << widget
          << " widget_state=" << WidgetStateToString(state);

  for (auto& it : video_windows_) {
    if (it.second->GetOwnerWidget() == widget)
      it.second->OnOwnerWidgetStateChanged(state);
  }
}

void VideoWindowProviderImpl::OwnerWidgetClosed(gfx::AcceleratedWidget widget) {
  if (task_runner_ && !task_runner_->BelongsToCurrentThread()) {
    task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&VideoWindowProviderImpl::OwnerWidgetClosed,
                                  weak_this_, widget));
    return;
  }

  VLOG(1) << __func__ << " widget=" << widget;

  for (auto& it : video_windows_) {
    if (it.second->GetOwnerWidget() == widget && delegate_)
      delegate_->DestroyVideoWindow(it.first);
  }
}

void VideoWindowProviderImpl::OnVideoWindowCreated(
    bool success,
    const base::UnguessableToken& window_id,
    ui::VideoWindow* video_window) {
  if (task_runner_ && !task_runner_->BelongsToCurrentThread()) {
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&VideoWindowProviderImpl::OnVideoWindowCreated,
                       weak_this_, success, window_id, video_window));
    return;
  }

  if (success && video_window) {
    VLOG(1) << __func__ << " sucess: " << success
            << " / window_id: " << window_id
            << " / native_id: " << video_window->GetNativeWindowId();
    video_windows_[video_window->GetWindowId()]->SetVideoWindow(video_window);
    if (video_window_controller_)
      video_window_controller_->OnVideoWindowCreated(window_id, true);
  } else {
    LOG(ERROR) << __func__ << " sucess: " << success
               << " / window_id: " << window_id;
    if (video_window_controller_)
      video_window_controller_->OnVideoWindowCreated(window_id, false);
  }
}

void VideoWindowProviderImpl::OnVideoWindowDestroyed(
    const base::UnguessableToken& window_id) {
  if (task_runner_ && !task_runner_->BelongsToCurrentThread()) {
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&VideoWindowProviderImpl::OnVideoWindowDestroyed,
                       weak_this_, window_id));
    return;
  }

  video_windows_.erase(window_id);

  if (video_window_controller_)
    video_window_controller_->OnVideoWindowDestroyed(window_id);
}

}  // namespace ui
