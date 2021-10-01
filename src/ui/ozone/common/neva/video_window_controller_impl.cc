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

#include "ui/ozone/common/neva/video_window_controller_impl.h"

#include "base/bind.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace ui {

class VideoWindowControllerImpl::VideoWindowInfo {
 public:
  explicit VideoWindowInfo(gfx::AcceleratedWidget w,
                           const base::UnguessableToken& id,
                           const VideoWindowParams& params)
      : owner_widget_(w), id_(id), params_(params) {}
  VideoWindowInfo(const VideoWindowInfo&) = delete;
  VideoWindowInfo& operator=(const VideoWindowInfo&) = delete;

  ~VideoWindowInfo() = default;

  gfx::AcceleratedWidget owner_widget_;
  base::UnguessableToken id_;
  base::Optional<bool> visibility_ = base::nullopt;
  VideoWindowParams params_;
};

VideoWindowControllerImpl::VideoWindowControllerImpl() = default;

VideoWindowControllerImpl::~VideoWindowControllerImpl() = default;

void VideoWindowControllerImpl::Initialize(
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner) {
  task_runner_ = task_runner;
  initialized_ = true;
}

void VideoWindowControllerImpl::SetVideoWindowProvider(
    VideoWindowProvider* provider) {
  provider_ = provider;
}

void VideoWindowControllerImpl::Bind(
    mojo::PendingReceiver<mojom::VideoWindowConnector> receiver) {
  receiver_.Bind(std::move(receiver));
}

void VideoWindowControllerImpl::OnVideoWindowCreated(
    const base::UnguessableToken& window_id,
    bool success) {
  VLOG(1) << __func__ << " window_id=" << window_id;
  VideoWindowInfo* info = FindVideoWindowInfo(window_id);
  if (!info) {
    LOG(WARNING) << __func__ << " failed to find video window for "
                 << window_id;
    return;
  }

  if (!success)
    OnVideoWindowDestroyed(window_id);
}

void VideoWindowControllerImpl::OnVideoWindowDestroyed(
    const base::UnguessableToken& window_id) {
  VLOG(1) << __func__ << " window_id=" << window_id;
  VideoWindowInfo* info = FindVideoWindowInfo(window_id);
  if (!info) {
    LOG(WARNING) << __func__ << " failed to find video window for "
                 << window_id;
    return;
  }
  RemoveVideoWindowInfo(window_id);
}

void VideoWindowControllerImpl::CreateVideoWindow(
    gfx::AcceleratedWidget widget,
    mojo::PendingRemote<mojom::VideoWindowClient> client,
    mojo::PendingReceiver<mojom::VideoWindow> receiver,
    const VideoWindowParams& params) {
  base::UnguessableToken window_id = base::UnguessableToken::Create();
  VideoWindowInfo* info = new VideoWindowInfo(widget, window_id, params);
  id_to_widget_map_[window_id] = widget;
  VideoWindowInfoList& list = video_windows_[widget];
  list.emplace_back(info);

  if (!provider_) {
    LOG(ERROR) << "Not initialized.";
    return;
  }

  provider_->CreateVideoWindow(widget, window_id, std::move(client),
                               std::move(receiver), params);
}

bool VideoWindowControllerImpl::IsInitialized() {
  return initialized_;
}

void VideoWindowControllerImpl::NotifyVideoWindowGeometryChanged(
    gfx::AcceleratedWidget widget,
    const base::UnguessableToken& window_id,
    const gfx::Rect& rect) {
  VLOG(1) << __func__ << " window_id=" << window_id
          << " rect=" << rect.ToString();
  if (!provider_) {
    LOG(ERROR) << "Not initialized.";
    return;
  }

  auto it = hidden_candidate_.find(widget);
  if (it != hidden_candidate_.end()) {
    auto& hidden_candidate = it->second;
    hidden_candidate.erase(window_id);
  }

  SetVideoWindowVisibility(window_id, true);

  VideoWindowInfo* info = FindVideoWindowInfo(window_id);
  if (info && info->params_.use_overlay_processor_layout)
    provider_->VideoWindowGeometryChanged(window_id, rect);
}

void VideoWindowControllerImpl::BeginOverlayProcessor(
    gfx::AcceleratedWidget widget) {
  auto wl_it = video_windows_.find(widget);
  if (wl_it == video_windows_.end())
    return;

  // We are finding hidden video window by following way:
  // 1. Collect all video windows in a widget.
  //    e.g. [window1, window2, window3]
  // 2. Check occurence of NotifyVideoWindowGeometryChanged().
  //    e.g. occurences: [window1, window3]
  // 3. Treat residual windows as invisible windows.
  //    e.g. invisible windows: [window2]
  std::set<base::UnguessableToken>& hidden_candidate =
      hidden_candidate_[widget];
  hidden_candidate.clear();

  for (auto const& window : wl_it->second)
    if (window->visibility_.has_value() && window->visibility_.value())
      hidden_candidate.insert(window->id_);
}

void VideoWindowControllerImpl::EndOverlayProcessor(
    gfx::AcceleratedWidget widget) {
  auto wl_it = video_windows_.find(widget);
  if (wl_it == video_windows_.end())
    return;

  std::set<base::UnguessableToken>& hidden = hidden_candidate_[widget];
  for (auto id : hidden)
    SetVideoWindowVisibility(id, false);
  hidden_candidate_.erase(widget);
}

VideoWindowControllerImpl::VideoWindowInfo*
VideoWindowControllerImpl::FindVideoWindowInfo(
    const base::UnguessableToken& window_id) {
  auto widget_it = id_to_widget_map_.find(window_id);
  if (widget_it == id_to_widget_map_.end())
    return nullptr;
  auto wl_it = video_windows_.find(widget_it->second);
  if (wl_it == video_windows_.end())
    return nullptr;
  for (auto& window : wl_it->second)
    if (window->id_ == window_id)
      return window.get();
  return nullptr;
}

void VideoWindowControllerImpl::RemoveVideoWindowInfo(
    const base::UnguessableToken& window_id) {
  auto widget_it = id_to_widget_map_.find(window_id);
  if (widget_it == id_to_widget_map_.end()) {
    LOG(INFO) << __func__ << " failed to find widget";
    return;
  }
  gfx::AcceleratedWidget w = widget_it->second;
  id_to_widget_map_.erase(window_id);

  auto wl_it = video_windows_.find(w);
  if (wl_it == video_windows_.end()) {
    LOG(INFO) << __func__ << " failed to find info for widget";
    return;
  }

  int count = wl_it->second.size();

  for (auto it = wl_it->second.cbegin(); it != wl_it->second.cend(); it++) {
    if ((*it)->id_ == window_id) {
      wl_it->second.erase(it);
      count--;
      break;
    }
  }

  LOG(INFO) << __func__ << " total # of windows:" << id_to_widget_map_.size()
            << " / # of windows of widget(" << w << "):" << count;
}

void VideoWindowControllerImpl::SetVideoWindowVisibility(
    const base::UnguessableToken& window_id,
    bool visibility) {
  // provider_ is already checked from
  // VideoWindowControllerImpl::NotifyVideoWindowGeometryChanged
  DCHECK(provider_);
  VideoWindowInfo* info = FindVideoWindowInfo(window_id);
  if (!info) {
    LOG(WARNING) << __func__ << " failed to find video window for "
                 << window_id;
    return;
  }

  bool visibility_changed = false;
  if (info->visibility_.has_value() && info->visibility_.value() != visibility)
    visibility_changed = true;

  info->visibility_ = visibility;

  if (visibility_changed)
    provider_->VideoWindowVisibilityChanged(window_id, visibility);
}

}  // namespace ui
