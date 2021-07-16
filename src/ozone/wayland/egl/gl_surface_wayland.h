// Copyright 2016 The Chromium Authors. All rights reserved.
// Copyright 2016-2018 LG Electronics, Inc.
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

#ifndef OZONE_WAYLAND_EGL_GL_SURFACE_WAYLAND_H_
#define OZONE_WAYLAND_EGL_GL_SURFACE_WAYLAND_H_

#include <memory>
#include <memory>

#include "base/timer/timer.h"
#include "components/viz/common/gpu/gpu_vsync_callback.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/vsync_provider.h"
#include "ui/gl/gl_surface_egl.h"

struct wl_output;
struct wp_presentation_feedback;

namespace ozonewayland {

// GLSurface class implementation for wayland.
class GLSurfaceWayland : public gl::NativeViewGLSurfaceEGL {
 public:
  explicit GLSurfaceWayland(unsigned widget);
  GLSurfaceWayland(const GLSurfaceWayland&) = delete;
  GLSurfaceWayland& operator=(const GLSurfaceWayland&) = delete;

  // gl::GLSurface:
  bool Resize(const gfx::Size& size,
              float scale_factor,
              const gfx::ColorSpace& color_space,
              bool has_alpha) override;
  EGLConfig GetConfig() override;
  gfx::SwapResult SwapBuffers(PresentationCallback callback) override;
  bool SupportsGpuVSync() const override;
  void SetVSyncCallback(viz::GpuVSyncCallback callback) override;
  void SetGpuVSyncEnabled(bool enabled) override;

  base::TimeTicks GetLastVSyncTime() const;
  base::TimeDelta GetLastInterval() const;

 private:
  ~GLSurfaceWayland() override;

  // NativeViewGLSurfaceEGL overrides:
  std::unique_ptr<gfx::VSyncProvider> CreateVsyncProviderInternal() override;

  static void OnFeedbackSyncOutput(
      void* data,
      struct wp_presentation_feedback* presentation_feedback,
      struct wl_output* output);
  static void OnFeedbackPresented(
      void* data,
      struct wp_presentation_feedback* presentation_feedback,
      uint32_t tv_sec_hi,
      uint32_t tv_sec_lo,
      uint32_t tv_nsec,
      uint32_t refresh_nsec,
      uint32_t seq_hi,
      uint32_t seq_lo,
      uint32_t flags);
  static void OnFeedbackDiscarded(
      void* data,
      struct wp_presentation_feedback* presentation_feedback);
  void StartWaitForVSync();
  void StartOrStopVSync();
  void OnVSync(base::TimeTicks vsync_time, base::TimeDelta vsync_interval);

  unsigned widget_;

  viz::GpuVSyncCallback vsync_callback_;
  std::set<struct wp_presentation_feedback*> wayland_presentation_feedbacks_;
  base::TimeDelta last_interval_ = base::TimeDelta::FromSecondsD(1 / 60.);
  base::TimeTicks last_vsync_time_ = base::TimeTicks::Now();
  base::TimeTicks last_notified_vsync_time_;
  base::OneShotTimer vsync_timer_;
  bool vsync_enabled_ = false;
};

}  // namespace ozonewayland

#endif  // OZONE_WAYLAND_EGL_GL_SURFACE_WAYLAND_H_
