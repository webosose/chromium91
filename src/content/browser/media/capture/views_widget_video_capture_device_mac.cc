// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/capture/views_widget_video_capture_device_mac.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/remote_cocoa/browser/scoped_cg_window_id.h"
#include "content/browser/media/capture/mouse_cursor_overlay_controller.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/desktop_media_id.h"

namespace content {

class ViewsWidgetVideoCaptureDeviceMac::UIThreadDelegate
    : public remote_cocoa::ScopedCGWindowID::Observer {
 public:
  UIThreadDelegate(uint32_t cg_window_id,
                   const base::WeakPtr<FrameSinkVideoCaptureDevice> device,
                   MouseCursorOverlayController* cursor_controller)
      : cg_window_id_(cg_window_id),
        device_task_runner_(base::ThreadTaskRunnerHandle::Get()),
        device_(device),
        cursor_controller_(cursor_controller) {
    // Note that the use of base::Unretained below is safe, because
    // |this| is destroyed by a task posted to the same thread task runner.
    GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&UIThreadDelegate::ResolveFrameSinkIdOnUIThread,
                       base::Unretained(this)));
  }

  ~UIThreadDelegate() final {
    // This is called by a task posted by ViewsWidgetVideoCaptureDeviceMac's
    // destructor.
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    if (scoped_cg_window_id_) {
      scoped_cg_window_id_->RemoveObserver(this);
      scoped_cg_window_id_ = nullptr;
    }
  }

  void ResolveFrameSinkIdOnUIThread() {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    DCHECK(!scoped_cg_window_id_);

    scoped_cg_window_id_ = remote_cocoa::ScopedCGWindowID::Get(cg_window_id_);
    if (scoped_cg_window_id_) {
      scoped_cg_window_id_->AddObserver(this);
      device_task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(&FrameSinkVideoCaptureDevice::OnTargetChanged, device_,
                         scoped_cg_window_id_->GetFrameSinkId()));
    } else {
      // It is entirely possible (although unlikely) that the window
      // corresponding to |cg_window_id| be destroyed between when the capture
      // source was selected and when this code is executed. If that happens,
      // the target is lost.
      device_task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(&FrameSinkVideoCaptureDevice::OnTargetPermanentlyLost,
                         device_));
    }
  }

 private:
  // remote_cocoa::ScopedCGWindowID::Observer:
  void OnScopedCGWindowIDDestroyed(uint32_t cg_window_id) override {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    // |scoped_cg_window_id_| promises to invalidate its weak pointers before
    // this method is called.
    DCHECK(!scoped_cg_window_id_);
    device_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&FrameSinkVideoCaptureDevice::OnTargetPermanentlyLost,
                       device_));
  }
  void OnScopedCGWindowIDMouseMoved(uint32_t cg_window_id,
                                    const gfx::PointF& location_in_window,
                                    const gfx::Size& window_size) override {
    cursor_controller_->SetTargetSize(window_size);
    cursor_controller_->OnMouseMoved(location_in_window);
  }

  const uint32_t cg_window_id_;
  const scoped_refptr<base::SingleThreadTaskRunner> device_task_runner_;

  // |scoped_cg_window_id_| may only be accessed on the UI thread. It is
  // non-nullptr if and only if |this| is registered as an observer to it.
  base::WeakPtr<remote_cocoa::ScopedCGWindowID> scoped_cg_window_id_;

  // |device_| may only be dereferenced by tasks posted to
  // |device_task_runner_|.
  base::WeakPtr<FrameSinkVideoCaptureDevice> device_;

  // Owned by FrameSinkVideoCaptureDevice. This will be valid for the life of
  // UIThreadDelegate because the UIThreadDelegate deleter task will be posted
  // to the UI thread before the MouseCursorOverlayController deleter task.
  // See similar behavior in WebContentsVideoCaptureDevice::FrameTracker.
  MouseCursorOverlayController* const cursor_controller_;
};

ViewsWidgetVideoCaptureDeviceMac::ViewsWidgetVideoCaptureDeviceMac(
    const DesktopMediaID& source_id)
    : weak_factory_(this) {
  ui_thread_delegate_ = std::make_unique<UIThreadDelegate>(
      source_id.id, weak_factory_.GetWeakPtr(), cursor_controller());
}

ViewsWidgetVideoCaptureDeviceMac::~ViewsWidgetVideoCaptureDeviceMac() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // Ensure that |ui_thread_delegate_| remove itself as an observer on the UI
  // thread, and destroy itself on that thread.
  GetUIThreadTaskRunner({})->DeleteSoon(FROM_HERE,
                                        std::move(ui_thread_delegate_));
}

}  // namespace content
