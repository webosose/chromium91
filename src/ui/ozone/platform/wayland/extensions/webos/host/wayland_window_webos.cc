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
//

#include "ui/ozone/platform/wayland/extensions/webos/host/wayland_window_webos.h"

#include "ui/ozone/platform/wayland/extensions/webos/host/wayland_extensions_webos.h"
#include "ui/ozone/platform/wayland/extensions/webos/host/webos_shell_surface_wrapper.h"
#include "ui/ozone/platform/wayland/host/extended_input_wrapper.h"
#include "ui/ozone/platform/wayland/host/input_panel.h"
#include "ui/ozone/platform/wayland/host/input_panel_manager.h"
#include "ui/ozone/platform/wayland/host/surface_group_compositor_wrapper.h"
#include "ui/ozone/platform/wayland/host/surface_group_wrapper.h"
#include "ui/ozone/platform/wayland/host/wayland_connection.h"
#include "ui/ozone/platform/wayland/host/wayland_input_method_context.h"

namespace ui {

WaylandWindowWebos::WaylandWindowWebos(PlatformWindowDelegate* delegate,
                                       WaylandConnection* connection,
                                       WaylandExtensionsWebos* webos_extensions)
    : WaylandToplevelWindow(delegate, connection),
      webos_extensions_(webos_extensions) {}

WaylandWindowWebos::~WaylandWindowWebos() = default;

WaylandInputMethodContext* WaylandWindowWebos::GetInputMethodContext() {
  return static_cast<WaylandInputMethodContext*>(
      delegate()->GetInputMethodContext());
}

WaylandExtensionsWebos* WaylandWindowWebos::GetWebosExtensions() {
  return webos_extensions_;
}

void WaylandWindowWebos::HandleExposed() {
  delegate()->OnWindowExposed();
}

void WaylandWindowWebos::HandleStateAboutToChange(PlatformWindowState state) {
  delegate()->OnWindowStateAboutToChange(state);
}

void WaylandWindowWebos::HandleCursorVisibilityChanged(bool is_visible) {
  delegate()->OnCursorVisibilityChanged(is_visible);
}

void WaylandWindowWebos::HandleInputPanelVisibilityChanged(bool is_visible) {
  delegate()->OnInputPanelVisibilityChanged(is_visible);
}

void WaylandWindowWebos::HandleInputPanelRectangleChange(std::int32_t x,
                                                         std::int32_t y,
                                                         std::uint32_t width,
                                                         std::uint32_t height) {
  delegate()->OnInputPanelRectChanged(x, y, width, height);
}

void WaylandWindowWebos::CreateGroup(const ui::WindowGroupConfiguration& config) {
  SurfaceGroupCompositorWrapper* surface_group_compositor =
      webos_extensions_->GetSurfaceGroupCompositor();

  if (surface_group_compositor)
    surface_group_ =
        surface_group_compositor->CreateSurfaceGroup(this, config.name);

  if (surface_group_) {
    surface_group_->AllowAnonymousLayers(config.is_anonymous);
    for (auto& layer : config.layers)
      surface_group_->CreateLayer(layer.name, layer.z_order);
    connection()->ScheduleFlush();
  }
}

void WaylandWindowWebos::AttachToGroup(const std::string& group_name,
                                       const std::string& layer_name) {
  SurfaceGroupCompositorWrapper* surface_group_compositor =
      webos_extensions_->GetSurfaceGroupCompositor();

  if (surface_group_compositor)
    surface_group_ = surface_group_compositor->GetSurfaceGroup(group_name);

  if (surface_group_) {
    surface_group_->AttachToLayer(this, layer_name);
    connection()->ScheduleFlush();
  }
}

void WaylandWindowWebos::FocusGroupOwner() {
  if (surface_group_) {
    surface_group_->FocusOwner();
    connection()->ScheduleFlush();
  }
}

void WaylandWindowWebos::FocusGroupLayer() {
  if (surface_group_) {
    surface_group_->FocusLayer();
    connection()->ScheduleFlush();
  }
}

void WaylandWindowWebos::DetachGroup() {
  if (surface_group_) {
    surface_group_->Detach(this);
    connection()->ScheduleFlush();
  }
}

void WaylandWindowWebos::ShowInputPanel() {
  InputPanelManager* input_panel_manager =
      webos_extensions_->GetInputPanelManager();

  if (input_panel_manager) {
    InputPanel* input_panel = input_panel_manager->GetInputPanel(this);

    if (input_panel) {
      input_panel->ShowInputPanel();
      connection()->ScheduleFlush();
    }
  }
}

void WaylandWindowWebos::HideInputPanel() {
  InputPanelManager* input_panel_manager =
    webos_extensions_->GetInputPanelManager();

  if (input_panel_manager) {
    InputPanel* input_panel = input_panel_manager->GetInputPanel(this);

    if (input_panel) {
      input_panel->HideInputPanel();
      connection()->ScheduleFlush();
    }
  }
}

void WaylandWindowWebos::SetTextInputInfo(
    const ui::TextInputInfo& text_input_info) {
  InputPanelManager* input_panel_manager =
      webos_extensions_->GetInputPanelManager();

  if (input_panel_manager) {
    InputPanel* input_panel = input_panel_manager->GetInputPanel(this);

    if (input_panel) {
      input_panel->SetTextInputInfo(text_input_info);
      connection()->ScheduleFlush();
    }
  }
}

void WaylandWindowWebos::SetSurroundingText(const std::string& text,
                                            std::size_t cursor_position,
                                            std::size_t anchor_position) {
  InputPanelManager* input_panel_manager =
      webos_extensions_->GetInputPanelManager();

  if (input_panel_manager) {
    InputPanel* input_panel = input_panel_manager->GetInputPanel(this);

    if (input_panel) {
      input_panel->SetSurroundingText(text, cursor_position, anchor_position);
      connection()->ScheduleFlush();
    }
  }
}

void WaylandWindowWebos::XInputActivate(const std::string& type) {
  ExtendedInputWrapper* extended_input = webos_extensions_->GetExtendedInput();

  if (extended_input) {
    extended_input->Activate(type);
    connection()->ScheduleFlush();
  }
}

void WaylandWindowWebos::XInputDeactivate() {
  ExtendedInputWrapper* extended_input = webos_extensions_->GetExtendedInput();

  if (extended_input) {
    extended_input->Deactivate();
    connection()->ScheduleFlush();
  }
}

void WaylandWindowWebos::XInputInvokeAction(std::uint32_t keysym,
                                            XInputKeySymbolType symbol_type,
                                            XInputEventType event_type) {
  ExtendedInputWrapper* extended_input = webos_extensions_->GetExtendedInput();

  if (extended_input) {
    extended_input->InvokeAction(keysym, symbol_type, event_type);
    connection()->ScheduleFlush();
  }
}

void WaylandWindowWebos::SetGroupKeyMask(KeyMask key_mask) {
  auto webos_shell_surface =
      static_cast<WebosShellSurfaceWrapper*>(shell_toplevel());

  if (webos_shell_surface) {
    webos_shell_surface->SetGroupKeyMask(key_mask);
    connection()->ScheduleFlush();
  }
}

void WaylandWindowWebos::SetKeyMask(KeyMask key_mask, bool set) {
  auto webos_shell_surface = static_cast<WebosShellSurfaceWrapper*>(
      shell_toplevel());

  if (webos_shell_surface) {
    webos_shell_surface->SetKeyMask(key_mask, set);
    connection()->ScheduleFlush();
  }
}

void WaylandWindowWebos::SetInputRegion(const std::vector<gfx::Rect>& region) {
  auto webos_shell_surface = static_cast<WebosShellSurfaceWrapper*>(
      shell_toplevel());

  if (webos_shell_surface) {
    webos_shell_surface->SetInputRegion(region);
    connection()->ScheduleFlush();
  }
}

void WaylandWindowWebos::SetWindowProperty(const std::string& name,
                                           const std::string& value) {
  auto webos_shell_surface = static_cast<WebosShellSurfaceWrapper*>(
      shell_toplevel());

  if (webos_shell_surface) {
    webos_shell_surface->SetWindowProperty(name, value);
    connection()->ScheduleFlush();
  }
}

}  // namespace ui
