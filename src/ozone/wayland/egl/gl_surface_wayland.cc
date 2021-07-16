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

#include "ozone/wayland/egl/gl_surface_wayland.h"

#include <utility>

#include "base/trace_event/trace_event.h"
#include "ozone/wayland/display.h"
#include "ozone/wayland/protocol/presentation-time-client-protocol.h"
#include "ozone/wayland/shell/shell_surface.h"
#include "ozone/wayland/window.h"
#include "third_party/khronos/EGL/egl.h"
#include "ui/ozone/common/egl_util.h"

namespace ozonewayland {

namespace {
class WaylandFrameVSyncProvider : public gfx::VSyncProvider {
 public:
  explicit WaylandFrameVSyncProvider(GLSurfaceWayland* surface)
      : surface_(surface) {}
  ~WaylandFrameVSyncProvider() override = default;

  // gfx::VSyncProvider
  void GetVSyncParameters(UpdateVSyncCallback callback) override {
    std::move(callback).Run(surface_->GetLastVSyncTime(),
                            surface_->GetLastInterval());
  }
  bool GetVSyncParametersIfAvailable(base::TimeTicks* timebase,
                                     base::TimeDelta* interval) override {
    *timebase = surface_->GetLastVSyncTime();
    *interval = surface_->GetLastInterval();
    return true;
  }
  bool SupportGetVSyncParametersIfAvailable() const override { return true; }
  bool IsHWClock() const override { return true; }

 private:
  GLSurfaceWayland* surface_;
};
}  // namespace

GLSurfaceWayland::GLSurfaceWayland(unsigned widget)
    : NativeViewGLSurfaceEGL(
          reinterpret_cast<EGLNativeWindowType>(
              WaylandDisplay::GetInstance()->GetEglWindow(widget)),
          nullptr),
      widget_(widget) {}

bool GLSurfaceWayland::Resize(const gfx::Size& size,
                              float scale_factor,
                              const gfx::ColorSpace& color_space,
                              bool has_alpha) {
  if (size_ == size)
    return true;

  WaylandWindow* window = WaylandDisplay::GetInstance()->GetWindow(widget_);
  DCHECK(window);
  window->Resize(size.width(), size.height());
  size_ = size;
  return true;
}

EGLConfig GLSurfaceWayland::GetConfig() {
  if (!config_) {
    EGLint config_attribs[] = {EGL_BUFFER_SIZE,
                               32,
                               EGL_ALPHA_SIZE,
                               8,
                               EGL_BLUE_SIZE,
                               8,
                               EGL_GREEN_SIZE,
                               8,
                               EGL_RED_SIZE,
                               8,
                               EGL_RENDERABLE_TYPE,
                               EGL_OPENGL_ES2_BIT,
                               EGL_SURFACE_TYPE,
                               EGL_WINDOW_BIT,
                               EGL_NONE};
    config_ = ui::ChooseEGLConfig(GetDisplay(), config_attribs);
  }
  return config_;
}

base::TimeTicks GLSurfaceWayland::GetLastVSyncTime() const {
  return last_vsync_time_;
}

base::TimeDelta GLSurfaceWayland::GetLastInterval() const {
  return last_interval_;
}

std::unique_ptr<gfx::VSyncProvider>
GLSurfaceWayland::CreateVsyncProviderInternal() {
  return std::make_unique<WaylandFrameVSyncProvider>(this);
};

void GLSurfaceWayland::StartWaitForVSync() {
  base::TimeTicks now = base::TimeTicks::Now();
  base::TimeTicks next_time_tick =
      now.SnappedToNextTick(last_vsync_time_, last_interval_);
  base::TimeDelta next_vsync_wait = next_time_tick - now;

  vsync_timer_.Start(
      FROM_HERE, next_vsync_wait,
      BindOnce(&GLSurfaceWayland::OnVSync, base::Unretained(this),
               next_time_tick, last_interval_));
}

void GLSurfaceWayland::OnFeedbackSyncOutput(void* data,
                                            struct wp_presentation_feedback*,
                                            struct wl_output*) {}

void GLSurfaceWayland::OnFeedbackPresented(
    void* data,
    struct wp_presentation_feedback* presentation_feedback,
    uint32_t tv_sec_hi,
    uint32_t tv_sec_lo,
    uint32_t tv_nsec,
    uint32_t refresh_nsec,
    uint32_t /* seq_hi */,
    uint32_t /* seq_lo */,
    uint32_t /* flags */) {
  TRACE_EVENT0("wayland", "GLSurfaceWayland::OnFeedbackPresented");
  GLSurfaceWayland* self = static_cast<GLSurfaceWayland*>(data);
  DCHECK(self);

  wp_presentation_feedback_destroy(presentation_feedback);
  self->wayland_presentation_feedbacks_.erase(presentation_feedback);

  uint64_t vsync_seconds = (static_cast<int64_t>(tv_sec_hi) << 32) + tv_sec_lo;
  uint64_t vsync_microseconds =
      vsync_seconds * base::Time::kMicrosecondsPerSecond +
      tv_nsec / base::Time::kNanosecondsPerMicrosecond;
  self->last_vsync_time_ =
      base::TimeTicks() + base::TimeDelta::FromMicroseconds(vsync_microseconds);
  TRACE_EVENT_INSTANT_WITH_TIMESTAMP0("wayland", "WaylandFeedbackPresented",
                                      TRACE_EVENT_SCOPE_GLOBAL,
                                      self->last_vsync_time_);
  if (refresh_nsec > 0)
    self->last_interval_ = base::TimeDelta::FromNanoseconds(refresh_nsec);
  self->OnVSync(self->last_vsync_time_, self->last_interval_);
}

void GLSurfaceWayland::OnFeedbackDiscarded(
    void* data,
    struct wp_presentation_feedback* presentation_feedback) {
  TRACE_EVENT0("wayland", "GLSurfaceWayland::OnFeedbackDiscarded");
  GLSurfaceWayland* self = static_cast<GLSurfaceWayland*>(data);
  DCHECK(self);

  wp_presentation_feedback_destroy(presentation_feedback);
  self->wayland_presentation_feedbacks_.erase(presentation_feedback);
}

gfx::SwapResult GLSurfaceWayland::SwapBuffers(PresentationCallback callback) {
  VLOG(3) << __func__ << " widget=" << widget_
          << " time=" << base::TimeTicks::Now();
  // install frame callback before eglSwapBuffers
  WaylandWindow* window = WaylandDisplay::GetInstance()->GetWindow(widget_);
  struct wl_surface* wsurface = window->ShellSurface()->GetWLSurface();
  struct wp_presentation* presentation =
      WaylandDisplay::GetInstance()->GetPresentation();
  if (wsurface && presentation) {
    struct wp_presentation_feedback* presentation_feedback =
        wp_presentation_feedback(presentation, wsurface);
    wayland_presentation_feedbacks_.insert(presentation_feedback);
    static const struct wp_presentation_feedback_listener
        presentation_listener = {&GLSurfaceWayland::OnFeedbackSyncOutput,
                                 &GLSurfaceWayland::OnFeedbackPresented,
                                 &GLSurfaceWayland::OnFeedbackDiscarded};
    wp_presentation_feedback_add_listener(presentation_feedback,
                                          &presentation_listener, this);
  }
  gfx::SwapResult result =
      NativeViewGLSurfaceEGL::SwapBuffers(std::move(callback));
  WaylandDisplay::GetInstance()->FlushDisplay();
  return result;
}

bool GLSurfaceWayland::SupportsGpuVSync() const {
  return WaylandDisplay::GetInstance()->GetPresentation() != nullptr;
}

void GLSurfaceWayland::StartOrStopVSync() {
  if (vsync_callback_ && vsync_enabled_)
    StartWaitForVSync();
  else
    vsync_timer_.Stop();
}

void GLSurfaceWayland::SetVSyncCallback(viz::GpuVSyncCallback callback) {
  VLOG(2) << __func__ << " widget=" << widget_;
  vsync_callback_ = std::move(callback);
  StartOrStopVSync();
}

void GLSurfaceWayland::SetGpuVSyncEnabled(bool enabled) {
  if (vsync_enabled_ != enabled) {
    VLOG(2) << __func__ << " widget=" << widget_ << " enabled=" << enabled;
    vsync_enabled_ = enabled;
    StartOrStopVSync();
  }
}

void GLSurfaceWayland::OnVSync(base::TimeTicks vsync_time,
                               base::TimeDelta vsync_interval) {
  TRACE_EVENT2("wayland", "GLSurfaceWayland::OnVSync", "vsync_time_ms",
               (vsync_time - base::TimeTicks()).InMillisecondsF(),
               "vsync_interval_ms", vsync_interval.InMillisecondsF());
  if (vsync_enabled_ && vsync_callback_) {
    constexpr base::TimeDelta kMinimumVSyncDelta =
        base::TimeDelta::FromMilliseconds(2);
    if (vsync_time - last_notified_vsync_time_ > kMinimumVSyncDelta) {
      VLOG(3) << __func__ << " widget=" << widget_
              << " vsync_time=" << vsync_time;
      vsync_callback_.Run(vsync_time, vsync_interval);
      last_notified_vsync_time_ = vsync_time;
    }
    StartWaitForVSync();
  }
}

GLSurfaceWayland::~GLSurfaceWayland() {
  for (struct wp_presentation_feedback* presentation_feedback :
       wayland_presentation_feedbacks_)
    wp_presentation_feedback_destroy(presentation_feedback);
  wayland_presentation_feedbacks_.clear();

  // Destroy surface first
  Destroy();
  // Then wl egl window if window instance is still around
  WaylandWindow* window = WaylandDisplay::GetInstance()->GetWindow(widget_);
  if (window) {
    window->DestroyAcceleratedWidget();
    WaylandDisplay::GetInstance()->FlushDisplay();
  }
}

}  // namespace ozonewayland
