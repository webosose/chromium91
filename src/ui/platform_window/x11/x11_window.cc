// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/platform_window/x11/x11_window.h"

#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"
#include "build/chromeos_buildflags.h"
#include "net/base/network_interfaces.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "ui/base/buildflags.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/hit_test_x11.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/wm_role_names_linux.h"
#include "ui/base/x/x11_cursor.h"
#include "ui/base/x/x11_menu_registrar.h"
#include "ui/base/x/x11_os_exchange_data_provider.h"
#include "ui/base/x/x11_pointer_grab.h"
#include "ui/base/x/x11_topmost_window_finder.h"
#include "ui/base/x/x11_util.h"
#include "ui/display/screen.h"
#include "ui/events/devices/x11/touch_factory_x11.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/ozone/events_ozone.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/events/platform/x11/x11_event_source.h"
#include "ui/events/x/events_x_utils.h"
#include "ui/events/x/x11_event_translation.h"
#include "ui/gfx/skia_util.h"
#include "ui/gfx/x/x11_atom_cache.h"
#include "ui/gfx/x/x11_path.h"
#include "ui/gfx/x/x11_window_event_manager.h"
#include "ui/gfx/x/xproto.h"
#include "ui/gfx/x/xproto_util.h"
#include "ui/platform_window/common/platform_window_defaults.h"
#include "ui/platform_window/extensions/workspace_extension_delegate.h"
#include "ui/platform_window/extensions/x11_extension_delegate.h"
#include "ui/platform_window/wm/wm_drop_handler.h"
#include "ui/platform_window/x11/x11_topmost_window_finder.h"
#include "ui/platform_window/x11/x11_window_manager.h"

#if BUILDFLAG(USE_ATK)
#include "ui/platform_window/x11/atk_event_conversion.h"
#endif

namespace ui {

namespace {

// Opacity for drag widget windows.
constexpr float kDragWidgetOpacity = .75f;

// Coalesce touch/mouse events if needed
bool CoalesceEventsIfNeeded(const x11::Event& xev,
                            EventType type,
                            x11::Event* out) {
  if (xev.As<x11::MotionNotifyEvent>() ||
      (xev.As<x11::Input::DeviceEvent>() &&
       (type == ui::ET_TOUCH_MOVED || type == ui::ET_MOUSE_MOVED ||
        type == ui::ET_MOUSE_DRAGGED))) {
    return ui::CoalescePendingMotionEvents(xev, out) > 0;
  }
  return false;
}

int GetKeyModifiers(const XDragDropClient* client) {
  if (!client)
    return ui::XGetMaskAsEventFlags();
  return client->current_modifier_state();
}

// Special value of the _NET_WM_DESKTOP property which indicates that the window
// should appear on all workspaces/desktops.
const int32_t kAllWorkspaces = -1;

constexpr char kX11WindowRolePopup[] = "popup";
constexpr char kX11WindowRoleBubble[] = "bubble";
constexpr char kDarkGtkThemeVariant[] = "dark";

constexpr long kSystemTrayRequestDock = 0;

constexpr int kXembedInfoProtocolVersion = 0;
constexpr int kXembedFlagMap = 1 << 0;
constexpr int kXembedInfoFlags = kXembedFlagMap;

enum CrossingFlags : uint8_t {
  CROSSING_FLAG_FOCUS = 1 << 0,
  CROSSING_FLAG_SAME_SCREEN = 1 << 1,
};

// In some situations, views tries to make a zero sized window, and that
// makes us crash. Make sure we have valid sizes.
gfx::Rect SanitizeBounds(const gfx::Rect& bounds) {
  gfx::Size sanitized_size(std::max(bounds.width(), 1),
                           std::max(bounds.height(), 1));
  gfx::Rect sanitized_bounds(bounds.origin(), sanitized_size);
  return sanitized_bounds;
}

void SerializeImageRepresentation(const gfx::ImageSkiaRep& rep,
                                  std::vector<uint32_t>* data) {
  uint32_t width = rep.GetWidth();
  data->push_back(width);

  uint32_t height = rep.GetHeight();
  data->push_back(height);

  const SkBitmap& bitmap = rep.GetBitmap();

  for (uint32_t y = 0; y < height; ++y)
    for (uint32_t x = 0; x < width; ++x)
      data->push_back(bitmap.getColor(x, y));
}

x11::NotifyMode XI2ModeToXMode(x11::Input::NotifyMode xi2_mode) {
  switch (xi2_mode) {
    case x11::Input::NotifyMode::Normal:
      return x11::NotifyMode::Normal;
    case x11::Input::NotifyMode::Grab:
    case x11::Input::NotifyMode::PassiveGrab:
      return x11::NotifyMode::Grab;
    case x11::Input::NotifyMode::Ungrab:
    case x11::Input::NotifyMode::PassiveUngrab:
      return x11::NotifyMode::Ungrab;
    case x11::Input::NotifyMode::WhileGrabbed:
      return x11::NotifyMode::WhileGrabbed;
    default:
      NOTREACHED();
      return x11::NotifyMode::Normal;
  }
}

x11::NotifyDetail XI2DetailToXDetail(x11::Input::NotifyDetail xi2_detail) {
  switch (xi2_detail) {
    case x11::Input::NotifyDetail::Ancestor:
      return x11::NotifyDetail::Ancestor;
    case x11::Input::NotifyDetail::Virtual:
      return x11::NotifyDetail::Virtual;
    case x11::Input::NotifyDetail::Inferior:
      return x11::NotifyDetail::Inferior;
    case x11::Input::NotifyDetail::Nonlinear:
      return x11::NotifyDetail::Nonlinear;
    case x11::Input::NotifyDetail::NonlinearVirtual:
      return x11::NotifyDetail::NonlinearVirtual;
    case x11::Input::NotifyDetail::Pointer:
      return x11::NotifyDetail::Pointer;
    case x11::Input::NotifyDetail::PointerRoot:
      return x11::NotifyDetail::PointerRoot;
    case x11::Input::NotifyDetail::None:
      return x11::NotifyDetail::None;
  }
}

void SyncSetCounter(x11::Connection* connection,
                    x11::Sync::Counter counter,
                    int64_t value) {
  x11::Sync::Int64 sync_value{.hi = value >> 32, .lo = value & 0xFFFFFFFF};
  connection->sync().SetCounter({counter, sync_value});
}

// Returns the whole path from |window| to the root.
std::vector<x11::Window> GetParentsList(x11::Connection* connection,
                                        x11::Window window) {
  std::vector<x11::Window> result;
  while (window != x11::Window::None) {
    result.push_back(window);
    if (auto reply = connection->QueryTree({window}).Sync())
      window = reply->parent;
    else
      break;
  }
  return result;
}

}  // namespace

X11Window::X11Window(PlatformWindowDelegate* platform_window_delegate)
    : platform_window_delegate_(platform_window_delegate),
      connection_(x11::Connection::Get()),
      x_root_window_(GetX11RootWindow()) {
  DCHECK(connection_);
  DCHECK_NE(x_root_window_, x11::Window::None);

  // Set a class property key, which allows |this| to be used for interactive
  // events, e.g. move or resize.
  SetWmMoveResizeHandler(this, static_cast<WmMoveResizeHandler*>(this));

  // Set extensions property key that extends the interface of this platform
  // implementation.
  SetWorkspaceExtension(this, static_cast<WorkspaceExtension*>(this));
  SetX11Extension(this, static_cast<X11Extension*>(this));
}

X11Window::~X11Window() {
  PrepareForShutdown();
  Close();
}

void X11Window::Initialize(PlatformWindowInitProperties properties) {
  PlatformWindowOpacity opacity = properties.opacity;
  CreateXWindow(properties, opacity);

  // It can be a status icon window.  If it fails to initialize, don't provide
  // it with a native window handle, close ourselves and let the client destroy
  // ourselves.
  if (properties.wm_role_name == kStatusIconWmRoleName &&
      !InitializeAsStatusIcon()) {
    CloseXWindow();
    return;
  }

  // At this point, the X window is created.  Register it and notify the
  // platform window delegate.
  X11WindowManager::GetInstance()->AddWindow(this);

  connection_->AddEventObserver(this);
  DCHECK(X11EventSource::HasInstance());
  X11EventSource::GetInstance()->AddPlatformEventDispatcher(this);

  x11_window_move_client_ =
      std::make_unique<ui::X11DesktopWindowMoveClient>(this);

  // Mark the window as eligible for the move loop, which allows tab dragging.
  SetWmMoveLoopHandler(this, static_cast<WmMoveLoopHandler*>(this));

  platform_window_delegate_->OnAcceleratedWidgetAvailable(GetWidget());

  // TODO(erg): Maybe need to set a ViewProp here like in RWHL::RWHL().

  auto event_mask =
      x11::EventMask::ButtonPress | x11::EventMask::ButtonRelease |
      x11::EventMask::FocusChange | x11::EventMask::KeyPress |
      x11::EventMask::KeyRelease | x11::EventMask::EnterWindow |
      x11::EventMask::LeaveWindow | x11::EventMask::Exposure |
      x11::EventMask::VisibilityChange | x11::EventMask::StructureNotify |
      x11::EventMask::PropertyChange | x11::EventMask::PointerMotion;
  xwindow_events_ =
      std::make_unique<x11::XScopedEventSelector>(xwindow_, event_mask);
  connection_->Flush();

  if (IsXInput2Available())
    TouchFactory::GetInstance()->SetupXI2ForXWindow(xwindow_);

  // Request the _NET_WM_SYNC_REQUEST protocol which is used for synchronizing
  // between chrome and desktop compositor (or WM) during resizing.
  // The resizing behavior with _NET_WM_SYNC_REQUEST is:
  // 1. Desktop compositor (or WM) sends client message _NET_WM_SYNC_REQUEST
  //    with a 64 bits counter to notify about an incoming resize.
  // 2. Desktop compositor resizes chrome browser window.
  // 3. Desktop compositor waits on an alert on value change of XSyncCounter on
  //    chrome window.
  // 4. Chrome handles the ConfigureNotify event, and renders a new frame with
  //    the new size.
  // 5. Chrome increases the XSyncCounter on chrome window
  // 6. Desktop compositor gets the alert of counter change, and draws a new
  //    frame with new content from chrome.
  // 7. Desktop compositor responses user mouse move events, and starts a new
  //    resize process, go to step 1.
  std::vector<x11::Atom> protocols = {
      x11::GetAtom("WM_DELETE_WINDOW"),
      x11::GetAtom("_NET_WM_PING"),
      x11::GetAtom("_NET_WM_SYNC_REQUEST"),
  };
  SetArrayProperty(xwindow_, x11::GetAtom("WM_PROTOCOLS"), x11::Atom::ATOM,
                   protocols);

  // We need a WM_CLIENT_MACHINE value so we integrate with the desktop
  // environment.
  SetStringProperty(xwindow_, x11::Atom::WM_CLIENT_MACHINE, x11::Atom::STRING,
                    net::GetHostName());

  // Likewise, the X server needs to know this window's pid so it knows which
  // program to kill if the window hangs.
  // XChangeProperty() expects "pid" to be long.
  static_assert(sizeof(uint32_t) >= sizeof(pid_t),
                "pid_t should not be larger than uint32_t");
  uint32_t pid = getpid();
  x11::SetProperty(xwindow_, x11::GetAtom("_NET_WM_PID"), x11::Atom::CARDINAL,
                   pid);

  x11::Atom window_type;
  switch (properties.type) {
    case PlatformWindowType::kMenu:
      window_type = x11::GetAtom("_NET_WM_WINDOW_TYPE_MENU");
      break;
    case PlatformWindowType::kTooltip:
      window_type = x11::GetAtom("_NET_WM_WINDOW_TYPE_TOOLTIP");
      break;
    case PlatformWindowType::kPopup:
      window_type = x11::GetAtom("_NET_WM_WINDOW_TYPE_NOTIFICATION");
      break;
    case PlatformWindowType::kDrag:
      window_type = x11::GetAtom("_NET_WM_WINDOW_TYPE_DND");
      break;
    default:
      window_type = x11::GetAtom("_NET_WM_WINDOW_TYPE_NORMAL");
      break;
  }
  x11::SetProperty(xwindow_, x11::GetAtom("_NET_WM_WINDOW_TYPE"),
                   x11::Atom::ATOM, window_type);

  // The changes to |window_properties_| here will be sent to the X server just
  // before the window is mapped.

  // Remove popup windows from taskbar unless overridden.
  if ((properties.type == PlatformWindowType::kPopup ||
       properties.type == PlatformWindowType::kBubble) &&
      !properties.force_show_in_taskbar) {
    window_properties_.insert(x11::GetAtom("_NET_WM_STATE_SKIP_TASKBAR"));
  }

  // If the window should stay on top of other windows, add the
  // _NET_WM_STATE_ABOVE property.
  is_always_on_top_ = properties.keep_on_top;
  if (is_always_on_top_)
    window_properties_.insert(x11::GetAtom("_NET_WM_STATE_ABOVE"));

  workspace_ = base::nullopt;
  if (properties.visible_on_all_workspaces) {
    window_properties_.insert(x11::GetAtom("_NET_WM_STATE_STICKY"));
    x11::SetProperty(xwindow_, x11::GetAtom("_NET_WM_DESKTOP"),
                     x11::Atom::CARDINAL, kAllWorkspaces);
  } else if (!properties.workspace.empty()) {
    int32_t workspace;
    if (base::StringToInt(properties.workspace, &workspace))
      x11::SetProperty(xwindow_, x11::GetAtom("_NET_WM_DESKTOP"),
                       x11::Atom::CARDINAL, workspace);
  }

  if (!properties.wm_class_name.empty() || !properties.wm_class_class.empty()) {
    SetWindowClassHint(connection_, xwindow_, properties.wm_class_name,
                       properties.wm_class_class);
  }

  const char* wm_role_name = nullptr;
  // If the widget isn't overriding the role, provide a default value for popup
  // and bubble types.
  if (!properties.wm_role_name.empty()) {
    wm_role_name = properties.wm_role_name.c_str();
  } else {
    switch (properties.type) {
      case PlatformWindowType::kPopup:
        wm_role_name = kX11WindowRolePopup;
        break;
      case PlatformWindowType::kBubble:
        wm_role_name = kX11WindowRoleBubble;
        break;
      default:
        break;
    }
  }
  if (wm_role_name)
    SetWindowRole(xwindow_, std::string(wm_role_name));

  if (properties.remove_standard_frame) {
    // Setting _GTK_HIDE_TITLEBAR_WHEN_MAXIMIZED tells gnome-shell to not force
    // fullscreen on the window when it matches the desktop size.
    SetHideTitlebarWhenMaximizedProperty(xwindow_,
                                         HIDE_TITLEBAR_WHEN_MAXIMIZED);
  }

  if (properties.prefer_dark_theme) {
    SetStringProperty(xwindow_, x11::GetAtom("_GTK_THEME_VARIANT"),
                      x11::GetAtom("UTF8_STRING"), kDarkGtkThemeVariant);
  }

  if (IsSyncExtensionAvailable()) {
    x11::Sync::Int64 value{};
    update_counter_ = connection_->GenerateId<x11::Sync::Counter>();
    connection_->sync().CreateCounter({update_counter_, value});
    extended_update_counter_ = connection_->GenerateId<x11::Sync::Counter>();
    connection_->sync().CreateCounter({extended_update_counter_, value});

    std::vector<x11::Sync::Counter> counters{update_counter_,
                                             extended_update_counter_};

    // Set XSyncCounter as window property _NET_WM_SYNC_REQUEST_COUNTER. the
    // compositor will listen on them during resizing.
    SetArrayProperty(xwindow_, x11::GetAtom("_NET_WM_SYNC_REQUEST_COUNTER"),
                     x11::Atom::CARDINAL, counters);
  }

  // Always composite Chromium windows if a compositing WM is used.  Sometimes,
  // WMs will not composite fullscreen windows as an optimization, but this can
  // lead to tearing of fullscreen videos.
  x11::SetProperty<uint32_t>(xwindow_,
                             x11::GetAtom("_NET_WM_BYPASS_COMPOSITOR"),
                             x11::Atom::CARDINAL, 2);

  if (properties.icon)
    SetWindowIcons(gfx::ImageSkia(), *properties.icon);

  if (properties.type == PlatformWindowType::kDrag &&
      opacity == PlatformWindowOpacity::kTranslucentWindow) {
    SetOpacity(kDragWidgetOpacity);
  }

  SetWmDragHandler(this, this);

  drag_drop_client_ = std::make_unique<XDragDropClient>(this, window());
}

void X11Window::OnXWindowLostCapture() {
  platform_window_delegate_->OnLostCapture();
}

void X11Window::OnMouseEnter() {
  platform_window_delegate_->OnMouseEnter();
}

gfx::AcceleratedWidget X11Window::GetWidget() const {
  // In spite of being defined in Xlib as `unsigned long`, XID (|window()|'s
  // type) is fixed at 32-bits (CARD32) in X11 Protocol, therefore can't be
  // larger than 32 bits values on the wire (see https://crbug.com/607014 for
  // more details). So, It's safe to use static_cast here.
  return static_cast<gfx::AcceleratedWidget>(window());
}

void X11Window::Show(bool inactive) {
  if (window_mapped_in_client_)
    return;

  Map(inactive);
}

void X11Window::Hide() {
  if (!window_mapped_in_client_)
    return;

  // Make sure no resize task will run after the window is unmapped.
  CancelResize();

  WithdrawWindow(xwindow_);
  window_mapped_in_client_ = false;
}

void X11Window::Close() {
  if (is_shutting_down_)
    return;

  X11WindowManager::GetInstance()->RemoveWindow(this);

  is_shutting_down_ = true;

  CloseXWindow();

  platform_window_delegate_->OnClosed();
}

bool X11Window::IsVisible() const {
  // On Windows, IsVisible() returns true for minimized windows.  On X11, a
  // minimized window is not mapped, so an explicit IsMinimized() check is
  // necessary.
  return window_mapped_in_client_ || IsMinimized();
}

void X11Window::PrepareForShutdown() {
  connection_->RemoveEventObserver(this);
  DCHECK(X11EventSource::HasInstance());
  X11EventSource::GetInstance()->RemovePlatformEventDispatcher(this);
}

void X11Window::SetBounds(const gfx::Rect& bounds) {
  gfx::Rect new_bounds_in_pixels(bounds.origin(),
                                 AdjustSizeForDisplay(bounds.size()));

  const bool size_changed =
      bounds_in_pixels_.size() != new_bounds_in_pixels.size();
  const bool origin_changed =
      bounds_in_pixels_.origin() != new_bounds_in_pixels.origin();

  // Assume that the resize will go through as requested, which should be the
  // case if we're running without a window manager.  If there's a window
  // manager, it can modify or ignore the request, but (per ICCCM) we'll get a
  // (possibly synthetic) ConfigureNotify about the actual size and correct
  // |bounds_| later.

  x11::ConfigureWindowRequest req{.window = xwindow_};

  if (size_changed) {
    // Only cancel the delayed resize task if we're already about to call
    // OnHostResized in this function.
    CancelResize();

    // Update the minimum and maximum sizes in case they have changed.
    UpdateMinAndMaxSize();

    if (new_bounds_in_pixels.width() < min_size_in_pixels_.width() ||
        new_bounds_in_pixels.height() < min_size_in_pixels_.height() ||
        (!max_size_in_pixels_.IsEmpty() &&
         (new_bounds_in_pixels.width() > max_size_in_pixels_.width() ||
          new_bounds_in_pixels.height() > max_size_in_pixels_.height()))) {
      gfx::Size size_in_pixels = new_bounds_in_pixels.size();
      if (!max_size_in_pixels_.IsEmpty())
        size_in_pixels.SetToMin(max_size_in_pixels_);
      size_in_pixels.SetToMax(min_size_in_pixels_);
      new_bounds_in_pixels.set_size(size_in_pixels);
    }

    req.width = new_bounds_in_pixels.width();
    req.height = new_bounds_in_pixels.height();
  }

  if (origin_changed) {
    req.x = new_bounds_in_pixels.x();
    req.y = new_bounds_in_pixels.y();
  }

  if (origin_changed || size_changed)
    connection_->ConfigureWindow(req);

  // Assume that the resize will go through as requested, which should be the
  // case if we're running without a window manager.  If there's a window
  // manager, it can modify or ignore the request, but (per ICCCM) we'll get a
  // (possibly synthetic) ConfigureNotify about the actual size and correct
  // |bounds_in_pixels_| later.
  bounds_in_pixels_ = new_bounds_in_pixels;
  ResetWindowRegion();

  // Even if the pixel bounds didn't change this call to the delegate should
  // still happen. The device scale factor may have changed which effectively
  // changes the bounds.
  OnXWindowBoundsChanged(new_bounds_in_pixels);
}

gfx::Rect X11Window::GetBounds() const {
  return bounds_in_pixels_;
}

void X11Window::SetTitle(const std::u16string& title) {
  if (window_title_ == title)
    return;

  window_title_ = title;
  std::string utf8str = base::UTF16ToUTF8(title);
  SetStringProperty(xwindow_, x11::GetAtom("_NET_WM_NAME"),
                    x11::GetAtom("UTF8_STRING"), utf8str);
  SetStringProperty(xwindow_, x11::Atom::WM_NAME, x11::GetAtom("UTF8_STRING"),
                    utf8str);
}

void X11Window::SetCapture() {
  if (HasCapture())
    return;
  X11WindowManager::GetInstance()->GrabEvents(this);

  // If the pointer is already in |xwindow_|, we will not get a crossing event
  // with a mode of NotifyGrab, so we must record the grab state manually.
  has_pointer_grab_ |=
      (ui::GrabPointer(xwindow_, true, nullptr) == x11::GrabStatus::Success);
}

void X11Window::ReleaseCapture() {
  if (!HasCapture())
    return;

  UngrabPointer();
  has_pointer_grab_ = false;

  X11WindowManager::GetInstance()->UngrabEvents(this);
}

bool X11Window::HasCapture() const {
  return X11WindowManager::GetInstance()->located_events_grabber() == this;
}

void X11Window::ToggleFullscreen() {
  // Check if we need to fullscreen the window or not.
  bool fullscreen = state_ != PlatformWindowState::kFullScreen;
  if (fullscreen)
    CancelResize();

  // Work around a bug where if we try to unfullscreen, metacity immediately
  // fullscreens us again. This is a little flickery and not necessary if
  // there's a gnome-panel, but it's not easy to detect whether there's a
  // panel or not.
  bool unmaximize_and_remaximize = !fullscreen && IsMaximized() &&
                                   ui::GuessWindowManager() == ui::WM_METACITY;

  if (unmaximize_and_remaximize)
    Restore();

  // Fullscreen state changes have to be handled manually and then checked
  // against configuration events, which come from a compositor. The reason
  // of manually changing the |state_| is that the compositor answers
  // about state changes asynchronously, which leads to a wrong return value in
  // DesktopWindowTreeHostPlatform::IsFullscreen, for example, and media
  // files can never be set to fullscreen. Wayland does the same.
  auto new_state = PlatformWindowState::kNormal;
  if (fullscreen)
    new_state = PlatformWindowState::kFullScreen;
  else if (IsMaximized())
    new_state = PlatformWindowState::kMaximized;

  bool was_fullscreen = IsFullscreen();
  state_ = new_state;
  SetFullscreen(fullscreen);

  if (unmaximize_and_remaximize)
    Maximize();

  // Try to guess the size we will have after the switch to/from fullscreen:
  // - (may) avoid transient states
  // - works around Flash content which expects to have the size updated
  //   synchronously.
  // See https://crbug.com/361408
  gfx::Rect new_bounds_px = GetBounds();
  if (fullscreen) {
    display::Screen* screen = display::Screen::GetScreen();
    const display::Display display = screen->GetDisplayMatching(new_bounds_px);
    SetRestoredBoundsInPixels(new_bounds_px);
    new_bounds_px =
        gfx::Rect(gfx::ScaleToFlooredPoint(display.bounds().origin(),
                                           display.device_scale_factor()),
                  display.GetSizeInPixel());
  } else {
    // Exiting "browser fullscreen mode", but the X11 window is not necessarily
    // in fullscreen state (e.g: a WM keybinding might have been used to toggle
    // fullscreen state). So check whether the window is in fullscreen state
    // before trying to restore its bounds (saved before entering in browser
    // fullscreen mode).
    if (was_fullscreen)
      new_bounds_px = GetRestoredBoundsInPixels();
    else
      SetRestoredBoundsInPixels({});
  }
  // Do not go through SetBounds as long as it adjusts bounds and sets them to X
  // Server. Instead, we just store the bounds and notify the client that the
  // window occupies the entire screen.
  bounds_in_pixels_ = new_bounds_px;
  platform_window_delegate_->OnBoundsChanged(new_bounds_px);
}

void X11Window::Maximize() {
  if (IsFullscreen()) {
    // Unfullscreen the window if it is fullscreen.
    SetFullscreen(false);

    // Resize the window so that it does not have the same size as a monitor.
    // (Otherwise, some window managers immediately put the window back in
    // fullscreen mode).
    gfx::Rect bounds_in_pixels = GetBounds();
    gfx::Rect adjusted_bounds_in_pixels(
        bounds_in_pixels.origin(),
        AdjustSizeForDisplay(bounds_in_pixels.size()));
    if (adjusted_bounds_in_pixels != bounds_in_pixels)
      SetBounds(adjusted_bounds_in_pixels);
  }

  // When we are in the process of requesting to maximize a window, we can
  // accurately keep track of our restored bounds instead of relying on the
  // heuristics that are in the PropertyNotify and ConfigureNotify handlers.
  SetRestoredBoundsInPixels(GetBounds());

  // Some WMs do not respect maximization hints on unmapped windows, so we
  // save this one for later too.
  should_maximize_after_map_ = !window_mapped_in_client_;

  SetWMSpecState(true, x11::GetAtom("_NET_WM_STATE_MAXIMIZED_VERT"),
                 x11::GetAtom("_NET_WM_STATE_MAXIMIZED_HORZ"));
}

void X11Window::Minimize() {
  if (window_mapped_in_client_) {
    SendClientMessage(xwindow_, x_root_window_, x11::GetAtom("WM_CHANGE_STATE"),
                      {WM_STATE_ICONIC, 0, 0, 0, 0});
  } else {
    SetWMSpecState(true, x11::GetAtom("_NET_WM_STATE_HIDDEN"), x11::Atom::None);
  }
}

void X11Window::Restore() {
  should_maximize_after_map_ = false;
  SetWMSpecState(false, x11::GetAtom("_NET_WM_STATE_MAXIMIZED_VERT"),
                 x11::GetAtom("_NET_WM_STATE_MAXIMIZED_HORZ"));
  SetWMSpecState(false, x11::GetAtom("_NET_WM_STATE_HIDDEN"), x11::Atom::None);
}

PlatformWindowState X11Window::GetPlatformWindowState() const {
  return state_;
}

void X11Window::Activate() {
  if (!IsVisible() || !activatable_)
    return;

  BeforeActivationStateChanged();

  ignore_keyboard_input_ = false;

  // wmii says that it supports _NET_ACTIVE_WINDOW but does not.
  // https://code.google.com/p/wmii/issues/detail?id=266
  static bool wm_supports_active_window =
      GuessWindowManager() != WM_WMII &&
      WmSupportsHint(x11::GetAtom("_NET_ACTIVE_WINDOW"));

  x11::Time timestamp = X11EventSource::GetInstance()->GetTimestamp();

  // override_redirect windows ignore _NET_ACTIVE_WINDOW.
  // https://crbug.com/940924
  if (wm_supports_active_window && !override_redirect_) {
    std::array<uint32_t, 5> data = {
        // We're an app.
        1,
        static_cast<uint32_t>(timestamp),
        // TODO(thomasanderson): if another chrome window is active, specify
        // that here.  The EWMH spec claims this may make the WM more likely to
        // service our _NET_ACTIVE_WINDOW request.
        0,
        0,
        0,
    };
    SendClientMessage(xwindow_, x_root_window_,
                      x11::GetAtom("_NET_ACTIVE_WINDOW"), data);
  } else {
    RaiseWindow(xwindow_);
    // Directly ask the X server to give focus to the window. Note that the call
    // would have raised an X error if the window is not mapped.
    connection_
        ->SetInputFocus({x11::InputFocus::Parent, xwindow_,
                         static_cast<x11::Time>(timestamp)})
        .IgnoreError();
    // At this point, we know we will receive focus, and some webdriver tests
    // depend on a window being IsActive() immediately after an Activate(), so
    // just set this state now.
    has_pointer_focus_ = false;
    has_window_focus_ = true;
    window_mapped_in_server_ = true;
  }

  AfterActivationStateChanged();
}

void X11Window::Deactivate() {
  BeforeActivationStateChanged();

  // Ignore future input events.
  ignore_keyboard_input_ = true;

  ui::LowerWindow(xwindow_);

  AfterActivationStateChanged();
}

void X11Window::SetUseNativeFrame(bool use_native_frame) {
  use_native_frame_ = use_native_frame;
  SetUseOSWindowFrame(xwindow_, use_native_frame);
  ResetWindowRegion();
}

bool X11Window::ShouldUseNativeFrame() const {
  return use_native_frame_;
}

void X11Window::SetCursor(PlatformCursor cursor) {
  DCHECK(cursor);

  last_cursor_ = static_cast<X11Cursor*>(cursor);
  on_cursor_loaded_.Reset(base::BindOnce(DefineCursor, xwindow_));
  last_cursor_->OnCursorLoaded(on_cursor_loaded_.callback());
}

void X11Window::MoveCursorTo(const gfx::Point& location_px) {
  connection_->WarpPointer(x11::WarpPointerRequest{
      .dst_window = x_root_window_,
      .dst_x = bounds_in_pixels_.x() + location_px.x(),
      .dst_y = bounds_in_pixels_.y() + location_px.y(),
  });
}

void X11Window::ConfineCursorToBounds(const gfx::Rect& bounds) {
  UnconfineCursor();

  if (bounds.IsEmpty())
    return;

  gfx::Rect barrier = bounds + bounds_in_pixels_.OffsetFromOrigin();

  auto make_barrier = [&](uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                          x11::XFixes::BarrierDirections directions) {
    x11::XFixes::Barrier barrier =
        connection_->GenerateId<x11::XFixes::Barrier>();
    connection_->xfixes().CreatePointerBarrier(
        {barrier, x_root_window_, x1, y1, x2, y2, directions});
    return barrier;
  };

  // Top horizontal barrier.
  pointer_barriers_[0] =
      make_barrier(barrier.x(), barrier.y(), barrier.right(), barrier.y(),
                   x11::XFixes::BarrierDirections::PositiveY);
  // Bottom horizontal barrier.
  pointer_barriers_[1] =
      make_barrier(barrier.x(), barrier.bottom(), barrier.right(),
                   barrier.bottom(), x11::XFixes::BarrierDirections::NegativeY);
  // Left vertical barrier.
  pointer_barriers_[2] =
      make_barrier(barrier.x(), barrier.y(), barrier.x(), barrier.bottom(),
                   x11::XFixes::BarrierDirections::PositiveX);
  // Right vertical barrier.
  pointer_barriers_[3] =
      make_barrier(barrier.right(), barrier.y(), barrier.right(),
                   barrier.bottom(), x11::XFixes::BarrierDirections::NegativeX);

  has_pointer_barriers_ = true;
}

void X11Window::SetRestoredBoundsInPixels(const gfx::Rect& bounds) {
  restored_bounds_in_pixels_ = bounds;
}

gfx::Rect X11Window::GetRestoredBoundsInPixels() const {
  return restored_bounds_in_pixels_;
}

bool X11Window::ShouldWindowContentsBeTransparent() const {
  return visual_has_alpha_;
}

void X11Window::SetZOrderLevel(ZOrderLevel order) {
  z_order_ = order;

  // Emulate the multiple window levels provided by other platforms by
  // collapsing the z-order enum into kNormal = normal, everything else = always
  // on top.
  is_always_on_top_ = (z_order_ != ui::ZOrderLevel::kNormal);
  SetWMSpecState(is_always_on_top_, x11::GetAtom("_NET_WM_STATE_ABOVE"),
                 x11::Atom::None);
}

ZOrderLevel X11Window::GetZOrderLevel() const {
  bool level_always_on_top = z_order_ != ui::ZOrderLevel::kNormal;

  if (is_always_on_top_ == level_always_on_top)
    return z_order_;

  // If something external has forced a window to be always-on-top, map it to
  // kFloatingWindow as a reasonable equivalent.
  return is_always_on_top_ ? ui::ZOrderLevel::kFloatingWindow
                           : ui::ZOrderLevel::kNormal;
}

void X11Window::StackAbove(gfx::AcceleratedWidget widget) {
  // Check comment in the GetWidget method about this cast.
  auto window = static_cast<x11::Window>(widget);
  DCHECK(window != x11::Window::None);

  // Find all parent windows up to the root.
  std::vector<x11::Window> window_below_parents =
      GetParentsList(connection_, window);
  std::vector<x11::Window> window_above_parents =
      GetParentsList(connection_, xwindow_);

  // Find their common ancestor.
  auto it_below_window = window_below_parents.rbegin();
  auto it_above_window = window_above_parents.rbegin();
  for (; it_below_window != window_below_parents.rend() &&
         it_above_window != window_above_parents.rend() &&
         *it_below_window == *it_above_window;
       ++it_below_window, ++it_above_window) {
  }

  if (it_below_window != window_below_parents.rend() &&
      it_above_window != window_above_parents.rend()) {
    connection_->ConfigureWindow(x11::ConfigureWindowRequest{
        .window = *it_above_window,
        .sibling = *it_below_window,
        .stack_mode = x11::StackMode::Above,
    });
  }
}

void X11Window::StackAtTop() {
  RaiseWindow(xwindow_);
}

void X11Window::FlashFrame(bool flash_frame) {
  SetFlashFrameHint(flash_frame);
}

void X11Window::SetShape(std::unique_ptr<ShapeRects> native_shape,
                         const gfx::Transform& transform) {
  std::unique_ptr<std::vector<x11::Rectangle>> xregion;
  if (native_shape) {
    SkRegion native_region;
    for (const gfx::Rect& rect : *native_shape)
      native_region.op(gfx::RectToSkIRect(rect), SkRegion::kUnion_Op);
    if (!transform.IsIdentity() && !native_region.isEmpty()) {
      SkPath path_in_dip;
      if (native_region.getBoundaryPath(&path_in_dip)) {
        SkPath path_in_pixels;
        path_in_dip.transform(SkMatrix(transform.matrix()), &path_in_pixels);
        xregion = x11::CreateRegionFromSkPath(path_in_pixels);
      } else {
        xregion = std::make_unique<std::vector<x11::Rectangle>>();
      }
    } else {
      xregion = x11::CreateRegionFromSkRegion(native_region);
    }
  }

  custom_window_shape_ = !!xregion;
  window_shape_ = std::move(xregion);
  ResetWindowRegion();
}

void X11Window::SetAspectRatio(const gfx::SizeF& aspect_ratio) {
  SizeHints size_hints;
  memset(&size_hints, 0, sizeof(size_hints));

  GetWmNormalHints(xwindow_, &size_hints);
  // Unforce aspect ratio is parameter length is 0, otherwise set normally.
  if (aspect_ratio.IsEmpty()) {
    size_hints.flags &= ~SIZE_HINT_P_ASPECT;
  } else {
    size_hints.flags |= SIZE_HINT_P_ASPECT;
    size_hints.min_aspect_num = size_hints.max_aspect_num =
        aspect_ratio.width();
    size_hints.min_aspect_den = size_hints.max_aspect_den =
        aspect_ratio.height();
  }
  SetWmNormalHints(xwindow_, size_hints);
}

void X11Window::SetWindowIcons(const gfx::ImageSkia& window_icon,
                               const gfx::ImageSkia& app_icon) {
  // TODO(erg): The way we handle icons across different versions of chrome
  // could be substantially improved. The Windows version does its own thing
  // and only sometimes comes down this code path. The icon stuff in
  // ChromeViewsDelegate is hard coded to use HICONs. Likewise, we're hard
  // coded to be given two images instead of an arbitrary collection of images
  // so that we can pass to the WM.
  //
  // All of this could be made much, much better.
  std::vector<uint32_t> data;

  if (!window_icon.isNull())
    SerializeImageRepresentation(window_icon.GetRepresentation(1.0f), &data);

  if (!app_icon.isNull())
    SerializeImageRepresentation(app_icon.GetRepresentation(1.0f), &data);

  if (!data.empty()) {
    SetArrayProperty(xwindow_, x11::GetAtom("_NET_WM_ICON"),
                     x11::Atom::CARDINAL, data);
  }
}

void X11Window::SizeConstraintsChanged() {
  X11Window::UpdateMinAndMaxSize();
}

bool X11Window::IsTranslucentWindowOpacitySupported() const {
  // This function may be called before InitX11Window() (which
  // initializes |visual_has_alpha_|), so we cannot simply return
  // |visual_has_alpha_|.
  return ui::XVisualManager::GetInstance()->ArgbVisualAvailable();
}

void X11Window::SetOpacity(float opacity) {
  // X server opacity is in terms of 32 bit unsigned int space, and counts from
  // the opposite direction.
  // XChangeProperty() expects "cardinality" to be long.

  // Scale opacity to [0 .. 255] range.
  uint32_t opacity_8bit = static_cast<uint32_t>(opacity * 255.0f) & 0xFF;
  // Use opacity value for all channels.
  uint32_t channel_multiplier = 0x1010101;
  uint32_t cardinality = opacity_8bit * channel_multiplier;

  if (cardinality == 0xffffffff) {
    x11::DeleteProperty(xwindow_, x11::GetAtom("_NET_WM_WINDOW_OPACITY"));
  } else {
    x11::SetProperty(xwindow_, x11::GetAtom("_NET_WM_WINDOW_OPACITY"),
                     x11::Atom::CARDINAL, cardinality);
  }
}

std::string X11Window::GetWorkspace() const {
  base::Optional<int> workspace_id = workspace_;
  return workspace_id.has_value() ? base::NumberToString(workspace_id.value())
                                  : std::string();
}

void X11Window::SetVisibleOnAllWorkspaces(bool always_visible) {
  SetWMSpecState(always_visible, x11::GetAtom("_NET_WM_STATE_STICKY"),
                 x11::Atom::None);

  int new_desktop = 0;
  if (always_visible) {
    new_desktop = kAllWorkspaces;
  } else {
    if (!GetCurrentDesktop(&new_desktop))
      return;
  }

  workspace_ = kAllWorkspaces;
  SendClientMessage(xwindow_, x_root_window_, x11::GetAtom("_NET_WM_DESKTOP"),
                    {new_desktop, 0, 0, 0, 0});
}

bool X11Window::IsVisibleOnAllWorkspaces() const {
  // We don't need a check for _NET_WM_STATE_STICKY because that would specify
  // that the window remain in a fixed position even if the viewport scrolls.
  // This is different from the type of workspace that's associated with
  // _NET_WM_DESKTOP.
  return workspace_ == kAllWorkspaces;
}

void X11Window::SetWorkspaceExtensionDelegate(
    WorkspaceExtensionDelegate* delegate) {
  workspace_extension_delegate_ = delegate;
}

bool X11Window::IsSyncExtensionAvailable() const {
  return ui::IsSyncExtensionAvailable();
}

bool X11Window::IsWmTiling() const {
  return ui::IsWmTiling(ui::GuessWindowManager());
}

void X11Window::OnCompleteSwapAfterResize() {
  if (configure_counter_value_is_extended_) {
    if ((current_counter_value_ % 2) == 1) {
      // An increase 3 means that the frame was not drawn as fast as possible.
      // This can trigger different handling from the compositor.
      // Setting an even number to |extended_update_counter_| will trigger a
      // new resize.
      current_counter_value_ += 3;
      SyncSetCounter(connection_, extended_update_counter_,
                     current_counter_value_);
    }
    return;
  }

  if (configure_counter_value_ != 0) {
    SyncSetCounter(connection_, update_counter_, configure_counter_value_);
    configure_counter_value_ = 0;
  }
}

gfx::Rect X11Window::GetXRootWindowOuterBounds() const {
  return GetOuterBounds();
}

bool X11Window::ContainsPointInXRegion(const gfx::Point& point) const {
  if (!shape())
    return true;

  for (const auto& rect : *shape()) {
    if (gfx::Rect(rect.x, rect.y, rect.width, rect.height).Contains(point))
      return true;
  }
  return false;
}

void X11Window::LowerXWindow() {
  ui::LowerWindow(xwindow_);
}

void X11Window::SetOverrideRedirect(bool override_redirect) {
  bool remap = window_mapped_in_client_;
  if (remap)
    Hide();
  connection_->ChangeWindowAttributes(x11::ChangeWindowAttributesRequest{
      .window = xwindow_,
      .override_redirect = x11::Bool32(override_redirect),
  });
  if (remap) {
    Map();
    if (has_pointer_grab_)
      ChangeActivePointerGrabCursor(nullptr);
  }
}

void X11Window::SetX11ExtensionDelegate(X11ExtensionDelegate* delegate) {
  x11_extension_delegate_ = delegate;
}

bool X11Window::HandleAsAtkEvent(const x11::Event& x11_event, bool transient) {
#if !BUILDFLAG(USE_ATK)
  // TODO(crbug.com/1014934): Support ATK in Ozone/X11.
  NOTREACHED();
  return false;
#else
  if (!x11_extension_delegate_ || !x11_event.As<x11::KeyEvent>())
    return false;
  auto atk_key_event = AtkKeyEventFromXEvent(x11_event);
  return x11_extension_delegate_->OnAtkKeyEvent(atk_key_event.get(), transient);
#endif
}

void X11Window::OnEvent(const x11::Event& xev) {
  auto* prop = xev.As<x11::PropertyNotifyEvent>();
  auto* target_current_context = drag_drop_client_->target_current_context();
  if (prop && target_current_context &&
      prop->window == target_current_context->source_window()) {
    target_current_context->DispatchPropertyNotifyEvent(*prop);
  }

  HandleEvent(xev);
}

bool X11Window::CanDispatchEvent(const PlatformEvent& xev) {
  if (is_shutting_down_)
    return false;
  DCHECK_NE(window(), x11::Window::None);
  auto* dispatching_event = connection_->dispatching_event();
  return dispatching_event && IsTargetedBy(*dispatching_event);
}

uint32_t X11Window::DispatchEvent(const PlatformEvent& event) {
  TRACE_EVENT1("views", "X11PlatformWindow::Dispatch", "event->type()",
               event->type());

  DCHECK_NE(window(), x11::Window::None);
  DCHECK(event);

  auto& current_xevent = *connection_->dispatching_event();

  if (event->IsMouseEvent())
    X11WindowManager::GetInstance()->MouseOnWindow(this);
#if BUILDFLAG(USE_ATK)
  if (HandleAsAtkEvent(current_xevent,
                       current_xevent.window() == transient_window_)) {
    return POST_DISPATCH_STOP_PROPAGATION;
  }
#endif

  DispatchUiEvent(event, current_xevent);
  return POST_DISPATCH_STOP_PROPAGATION;
}

void X11Window::DispatchUiEvent(ui::Event* event, const x11::Event& xev) {
  auto* window_manager = X11WindowManager::GetInstance();
  DCHECK(window_manager);

  // Process X11-specific bits
  HandleEvent(xev);

  // If |event| is a located event (mouse, touch, etc) and another X11 window
  // is set as the current located events grabber, the |event| must be
  // re-routed to that grabber. Otherwise, just send the event.
  auto* located_events_grabber = window_manager->located_events_grabber();
  if (event->IsLocatedEvent() && located_events_grabber &&
      located_events_grabber != this) {
    if (event->IsMouseEvent() ||
        (event->IsTouchEvent() && event->type() == ui::ET_TOUCH_PRESSED)) {
      // Another X11Window has installed itself as capture. Translate the
      // event's location and dispatch to the other.
      ConvertEventLocationToTargetLocation(located_events_grabber->GetBounds(),
                                           GetBounds(),
                                           event->AsLocatedEvent());
    }
    return located_events_grabber->DispatchUiEvent(event, xev);
  }

  x11::Event last_xev;
  std::unique_ptr<ui::Event> last_motion;
  if (CoalesceEventsIfNeeded(xev, event->type(), &last_xev)) {
    last_motion = ui::BuildEventFromXEvent(last_xev);
    event = last_motion.get();
  }

  // If after CoalescePendingMotionEvents the type of xev is resolved to
  // UNKNOWN, i.e: xevent translation returns nullptr, don't dispatch the
  // event. TODO(804418): investigate why ColescePendingMotionEvents can
  // include mouse wheel events as well. Investigation showed that events on
  // Linux are checked with cmt-device path, and can include DT_CMT_SCROLL_
  // data. See more discussion in https://crrev.com/c/853953
  if (event) {
    UpdateWMUserTime(event);
    bool event_dispatched = false;
#if defined(USE_OZONE)
    if (features::IsUsingOzonePlatform()) {
      event_dispatched = true;
      DispatchEventFromNativeUiEvent(
          event, base::BindOnce(&PlatformWindowDelegate::DispatchEvent,
                                base::Unretained(platform_window_delegate())));
    }
#endif
#if defined(USE_X11)
    if (!event_dispatched)
      platform_window_delegate_->DispatchEvent(event);
#endif
  }
}

void X11Window::OnXWindowStateChanged() {
  // Determine the new window state information to be propagated to the client.
  // Note that the order of checks is important here, because window can have
  // several properties at the same time.
  auto new_state = PlatformWindowState::kNormal;
  if (IsMinimized())
    new_state = PlatformWindowState::kMinimized;
  else if (IsFullscreen())
    new_state = PlatformWindowState::kFullScreen;
  else if (IsMaximized())
    new_state = PlatformWindowState::kMaximized;

  // fullscreen state is set syschronously at ToggleFullscreen() and must be
  // kept and propagated to the client only when explicitly requested by upper
  // layers, as it means we are in "browser fullscreen mode" (where
  // decorations, omnibar, buttons, etc are hidden), which is different from
  // the case where the request comes from the window manager (or any other
  // process), handled by this method. In this case, we follow EWMH guidelines:
  // Optimize the whole application for fullscreen usage. Window decorations
  // (e.g. borders) should be hidden, but the functionalily of the application
  // should not change. Further details:
  // https://specifications.freedesktop.org/wm-spec/wm-spec-1.3.html
  bool browser_fullscreen_mode = state_ == PlatformWindowState::kFullScreen;
  bool window_fullscreen_mode = new_state == PlatformWindowState::kFullScreen;
  // So, we ignore fullscreen state transitions in 2 cases:
  // 1. If |new_state| is kFullScreen but |state_| is not, which means the
  // fullscreen request is coming from an external process. So the browser
  // window must occupies the entire screen but not transitioning to browser
  // fullscreen mode.
  // 2. if |state_| is kFullScreen but |new_state| is not, we have been
  // requested to exit fullscreen by other process (e.g: via WM keybinding),
  // in this case we must keep on "browser fullscreen mode" bug the platform
  // window gets back to its previous state (e.g: unmaximized, tiled in TWMs,
  // etc).
  if (window_fullscreen_mode != browser_fullscreen_mode)
    return;

  if (GetRestoredBoundsInPixels().IsEmpty()) {
    if (IsMaximized()) {
      // The request that we become maximized originated from a different
      // process. |bounds_in_pixels_| already contains our maximized bounds. Do
      // a best effort attempt to get restored bounds by setting it to our
      // previously set bounds (and if we get this wrong, we aren't any worse
      // off since we'd otherwise be returning our maximized bounds).
      SetRestoredBoundsInPixels(previous_bounds_in_pixels_);
    }
  } else if (!IsMaximized() && !IsFullscreen()) {
    // If we have restored bounds, but WM_STATE no longer claims to be
    // maximized or fullscreen, we should clear our restored bounds.
    SetRestoredBoundsInPixels(gfx::Rect());
  }

  if (new_state != state_) {
    state_ = new_state;
    platform_window_delegate_->OnWindowStateChanged(state_);
  }
}

void X11Window::OnXWindowDamageEvent(const gfx::Rect& damage_rect) {
  platform_window_delegate_->OnDamageRect(damage_rect);
}

void X11Window::OnXWindowBoundsChanged(const gfx::Rect& bounds) {
  platform_window_delegate_->OnBoundsChanged(bounds);
}

void X11Window::OnXWindowCloseRequested() {
  platform_window_delegate_->OnCloseRequest();
}

void X11Window::OnXWindowIsActiveChanged(bool active) {
  platform_window_delegate_->OnActivationChanged(active);
}

void X11Window::OnXWindowWorkspaceChanged() {
  if (workspace_extension_delegate_)
    workspace_extension_delegate_->OnWorkspaceChanged();
}

void X11Window::OnXWindowLostPointerGrab() {
  if (x11_extension_delegate_)
    x11_extension_delegate_->OnLostMouseGrab();
}

void X11Window::OnXWindowSelectionEvent(const x11::SelectionNotifyEvent& xev) {
  DCHECK(drag_drop_client_);
  drag_drop_client_->OnSelectionNotify(xev);
}

void X11Window::OnXWindowDragDropEvent(const x11::ClientMessageEvent& xev) {
  DCHECK(drag_drop_client_);
  drag_drop_client_->HandleXdndEvent(xev);
}

base::Optional<gfx::Size> X11Window::GetMinimumSizeForXWindow() {
  return platform_window_delegate_->GetMinimumSizeForWindow();
}

base::Optional<gfx::Size> X11Window::GetMaximumSizeForXWindow() {
  return platform_window_delegate_->GetMaximumSizeForWindow();
}

SkPath X11Window::GetWindowMaskForXWindow() {
  return platform_window_delegate_->GetWindowMaskForWindowShapeInPixels();
}

void X11Window::DispatchHostWindowDragMovement(
    int hittest,
    const gfx::Point& pointer_location_in_px) {
  int direction = HitTestToWmMoveResizeDirection(hittest);
  if (direction == -1)
    return;

  DoWMMoveResize(connection_, x_root_window_, xwindow_, pointer_location_in_px,
                 direction);
}

bool X11Window::RunMoveLoop(const gfx::Vector2d& drag_offset) {
  return x11_window_move_client_->RunMoveLoop(!HasCapture(), drag_offset);
}

void X11Window::EndMoveLoop() {
  x11_window_move_client_->EndMoveLoop();
}

bool X11Window::StartDrag(const OSExchangeData& data,
                          int operation,
                          gfx::NativeCursor cursor,
                          bool can_grab_pointer,
                          WmDragHandler::Delegate* delegate) {
  DCHECK(drag_drop_client_);
  DCHECK(!drag_handler_delegate_);

  drag_handler_delegate_ = delegate;
  drag_drop_client_->InitDrag(operation, &data);
  drag_operation_ = 0;
  notified_enter_ = false;

  drag_loop_ = std::make_unique<X11WholeScreenMoveLoop>(this);

  auto alive = weak_ptr_factory_.GetWeakPtr();
  const bool dropped =
      drag_loop_->RunMoveLoop(can_grab_pointer, last_cursor_, last_cursor_);
  if (!alive)
    return false;

  drag_loop_.reset();
  drag_handler_delegate_ = nullptr;
  return dropped;
}

void X11Window::CancelDrag() {
  QuitDragLoop();
}

std::unique_ptr<XTopmostWindowFinder> X11Window::CreateWindowFinder() {
  return std::make_unique<X11TopmostWindowFinder>();
}

int X11Window::UpdateDrag(const gfx::Point& screen_point) {
  WmDropHandler* drop_handler = GetWmDropHandler(*this);
  if (!drop_handler)
    return DragDropTypes::DRAG_NONE;

  DCHECK(drag_drop_client_);
  auto* target_current_context = drag_drop_client_->target_current_context();
  DCHECK(target_current_context);

  auto data = std::make_unique<OSExchangeData>(
      std::make_unique<XOSExchangeDataProvider>(
          drag_drop_client_->xwindow(),
          target_current_context->fetched_targets()));
  int suggested_operations = target_current_context->GetDragOperation();
  // KDE-based file browsers such as Dolphin change the drag operation depending
  // on whether alt/ctrl/shift was pressed. However once Chromium gets control
  // over the X11 events, the source application does no longer receive X11
  // events for key modifier changes, so the dnd operation gets stuck in an
  // incorrect state. Blink can only dnd-open files of type DRAG_COPY, so the
  // DRAG_COPY mask is added if the dnd object is a file.
  if (data->HasFile() && (suggested_operations & (DragDropTypes::DRAG_MOVE |
                                                  DragDropTypes::DRAG_LINK))) {
    suggested_operations |= DragDropTypes::DRAG_COPY;
  }

  XDragDropClient* source_client =
      XDragDropClient::GetForWindow(target_current_context->source_window());
  if (!notified_enter_) {
    drop_handler->OnDragEnter(gfx::PointF(screen_point), std::move(data),
                              suggested_operations,
                              GetKeyModifiers(source_client));
    notified_enter_ = true;
  }
  drag_operation_ = drop_handler->OnDragMotion(gfx::PointF(screen_point),
                                               suggested_operations,
                                               GetKeyModifiers(source_client));
  return drag_operation_;
}

void X11Window::UpdateCursor(
    DragDropTypes::DragOperation negotiated_operation) {
  DCHECK(drag_handler_delegate_);
  drag_handler_delegate_->OnDragOperationChanged(negotiated_operation);
}

void X11Window::OnBeginForeignDrag(x11::Window window) {
  notified_enter_ = false;
  source_window_events_ = std::make_unique<x11::XScopedEventSelector>(
      window, x11::EventMask::PropertyChange);
}

void X11Window::OnEndForeignDrag() {
  source_window_events_.reset();
}

void X11Window::OnBeforeDragLeave() {
  WmDropHandler* drop_handler = GetWmDropHandler(*this);
  if (!drop_handler)
    return;
  drop_handler->OnDragLeave();
  notified_enter_ = false;
}

int X11Window::PerformDrop() {
  WmDropHandler* drop_handler = GetWmDropHandler(*this);
  if (!drop_handler || !notified_enter_)
    return DragDropTypes::DRAG_NONE;

  // The drop data has been supplied on entering the window.  The drop handler
  // should have it since then.
  auto* target_current_context = drag_drop_client_->target_current_context();
  DCHECK(target_current_context);
  drop_handler->OnDragDrop({}, GetKeyModifiers(XDragDropClient::GetForWindow(
                                   target_current_context->source_window())));
  notified_enter_ = false;
  return drag_operation_;
}

void X11Window::EndDragLoop() {
  DCHECK(drag_handler_delegate_);

  drag_handler_delegate_->OnDragFinished(drag_operation_);
  drag_loop_->EndMoveLoop();
}

void X11Window::OnMouseMovement(const gfx::Point& screen_point,
                                int flags,
                                base::TimeTicks event_time) {
  drag_handler_delegate_->OnDragLocationChanged(screen_point);
  drag_drop_client_->HandleMouseMovement(screen_point, flags, event_time);
}

void X11Window::OnMouseReleased() {
  drag_drop_client_->HandleMouseReleased();
}

void X11Window::OnMoveLoopEnded() {
  drag_drop_client_->HandleMoveLoopEnded();
}

void X11Window::SetBoundsOnMove(const gfx::Rect& requested_bounds) {
  SetBounds(requested_bounds);
}

scoped_refptr<X11Cursor> X11Window::GetLastCursor() {
  return last_cursor_;
}

gfx::Size X11Window::GetSize() {
  return bounds_in_pixels_.size();
}

void X11Window::QuitDragLoop() {
  DCHECK(drag_loop_);
  drag_loop_->EndMoveLoop();
}

gfx::Size X11Window::AdjustSizeForDisplay(
    const gfx::Size& requested_size_in_pixels) {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  // We do not need to apply the workaround for the ChromeOS.
  return requested_size_in_pixels;
#else
  auto* screen = display::Screen::GetScreen();
  if (screen && !UseTestConfigForPlatformWindows()) {
    std::vector<display::Display> displays = screen->GetAllDisplays();
    // Compare against all monitor sizes. The window manager can move the window
    // to whichever monitor it wants.
    for (const auto& display : displays) {
      if (requested_size_in_pixels == display.GetSizeInPixel()) {
        return gfx::Size(requested_size_in_pixels.width() - 1,
                         requested_size_in_pixels.height() - 1);
      }
    }
  }

  // Do not request a 0x0 window size. It causes an XError.
  gfx::Size size_in_pixels = requested_size_in_pixels;
  size_in_pixels.SetToMax(gfx::Size(1, 1));
  return size_in_pixels;
#endif
}

void X11Window::ConvertEventLocationToTargetLocation(
    const gfx::Rect& target_window_bounds,
    const gfx::Rect& current_window_bounds,
    LocatedEvent* located_event) {
  // TODO(msisov): for ozone, we need to access PlatformScreen instead and get
  // the displays.
  auto* display = display::Screen::GetScreen();
  DCHECK(display);
  auto display_window_target =
      display->GetDisplayMatching(target_window_bounds);
  auto display_window_current =
      display->GetDisplayMatching(current_window_bounds);
  DCHECK_EQ(display_window_target.device_scale_factor(),
            display_window_current.device_scale_factor());

  ConvertEventLocationToTargetWindowLocation(target_window_bounds.origin(),
                                             current_window_bounds.origin(),
                                             located_event);
}

void X11Window::CreateXWindow(const PlatformWindowInitProperties& properties,
                              PlatformWindowOpacity& opacity) {
  auto bounds = properties.bounds;
  gfx::Size adjusted_size_in_pixels = AdjustSizeForDisplay(bounds.size());
  bounds.set_size(adjusted_size_in_pixels);
  const auto override_redirect =
      properties.x11_extension_delegate &&
      properties.x11_extension_delegate->IsOverrideRedirect(IsWmTiling());
  if (properties.type == PlatformWindowType::kDrag) {
    opacity = ui::IsCompositingManagerPresent()
                  ? PlatformWindowOpacity::kTranslucentWindow
                  : PlatformWindowOpacity::kOpaqueWindow;
  }

  workspace_extension_delegate_ = properties.workspace_extension_delegate;
  x11_extension_delegate_ = properties.x11_extension_delegate;

  // Ensure that the X11MenuRegistrar exists. The X11MenuRegistrar is
  // necessary to properly track menu windows.
  X11MenuRegistrar::Get();

  activatable_ = properties.activatable;

  x11::CreateWindowRequest req;
  req.bit_gravity = x11::Gravity::NorthWest;
  req.background_pixel = properties.background_color.has_value()
                             ? properties.background_color.value()
                             : connection_->default_screen().white_pixel;

  switch (properties.type) {
    case PlatformWindowType::kMenu:
      req.override_redirect = x11::Bool32(true);
      break;
    case PlatformWindowType::kTooltip:
      req.override_redirect = x11::Bool32(true);
      break;
    case PlatformWindowType::kPopup:
      req.override_redirect = x11::Bool32(true);
      break;
    case PlatformWindowType::kDrag:
      req.override_redirect = x11::Bool32(true);
      break;
    default:
      break;
  }
  // An in-activatable window should not interact with the system wm.
  if (!activatable_ || override_redirect)
    req.override_redirect = x11::Bool32(true);

#if BUILDFLAG(IS_CHROMEOS_ASH)
  req.override_redirect = x11::Bool32(UseTestConfigForPlatformWindows());
#endif

  override_redirect_ = req.override_redirect.has_value();

  bool enable_transparent_visuals;
  switch (opacity) {
    case PlatformWindowOpacity::kOpaqueWindow:
      enable_transparent_visuals = false;
      break;
    case PlatformWindowOpacity::kTranslucentWindow:
      enable_transparent_visuals = true;
      break;
    case PlatformWindowOpacity::kInferOpacity:
      enable_transparent_visuals = properties.type == PlatformWindowType::kDrag;
  }

  if (properties.wm_role_name == kStatusIconWmRoleName) {
    std::string atom_name =
        "_NET_SYSTEM_TRAY_S" +
        base::NumberToString(connection_->DefaultScreenId());
    auto selection = connection_->GetSelectionOwner({x11::GetAtom(atom_name)});
    if (auto reply = selection.Sync()) {
      x11::GetProperty(reply->owner, x11::GetAtom("_NET_SYSTEM_TRAY_VISUAL"),
                       &visual_id_);
    }
  }

  x11::VisualId visual_id = visual_id_;
  uint8_t depth = 0;
  x11::ColorMap colormap{};
  XVisualManager* visual_manager = XVisualManager::GetInstance();
  if (visual_id_ == x11::VisualId{} ||
      !visual_manager->GetVisualInfo(visual_id_, &depth, &colormap,
                                     &visual_has_alpha_)) {
    visual_manager->ChooseVisualForWindow(enable_transparent_visuals,
                                          &visual_id, &depth, &colormap,
                                          &visual_has_alpha_);
  }

  // x.org will BadMatch if we don't set a border when the depth isn't the
  // same as the parent depth.
  req.border_pixel = 0;

  bounds_in_pixels_ = SanitizeBounds(bounds);
  req.parent = x_root_window_;
  req.x = bounds_in_pixels_.x();
  req.y = bounds_in_pixels_.y();
  req.width = bounds_in_pixels_.width();
  req.height = bounds_in_pixels_.height();
  req.depth = depth;
  req.c_class = x11::WindowClass::InputOutput;
  req.visual = visual_id;
  req.colormap = colormap;
  xwindow_ = connection_->GenerateId<x11::Window>();
  req.wid = xwindow_;
  connection_->CreateWindow(req);
}

void X11Window::CloseXWindow() {
  if (xwindow_ == x11::Window::None)
    return;

  CancelResize();
  UnconfineCursor();

  connection_->DestroyWindow({xwindow_});
  xwindow_ = x11::Window::None;

  if (update_counter_ != x11::Sync::Counter{}) {
    connection_->sync().DestroyCounter({update_counter_});
    connection_->sync().DestroyCounter({extended_update_counter_});
    update_counter_ = {};
    extended_update_counter_ = {};
  }
}

void X11Window::Map(bool inactive) {
  // Before we map the window, set size hints. Otherwise, some window managers
  // will ignore toplevel XMoveWindow commands.
  SizeHints size_hints;
  memset(&size_hints, 0, sizeof(size_hints));
  GetWmNormalHints(xwindow_, &size_hints);
  size_hints.flags |= SIZE_HINT_P_POSITION;
  size_hints.x = bounds_in_pixels_.x();
  size_hints.y = bounds_in_pixels_.y();
  SetWmNormalHints(xwindow_, size_hints);

  ignore_keyboard_input_ = inactive;
  auto wm_user_time_ms = ignore_keyboard_input_
                             ? x11::Time::CurrentTime
                             : X11EventSource::GetInstance()->GetTimestamp();
  if (inactive || wm_user_time_ms != x11::Time::CurrentTime) {
    x11::SetProperty(xwindow_, x11::GetAtom("_NET_WM_USER_TIME"),
                     x11::Atom::CARDINAL, wm_user_time_ms);
  }

  UpdateMinAndMaxSize();

  if (window_properties_.empty()) {
    x11::DeleteProperty(xwindow_, x11::GetAtom("_NET_WM_STATE"));
  } else {
    SetArrayProperty(xwindow_, x11::GetAtom("_NET_WM_STATE"), x11::Atom::ATOM,
                     std::vector<x11::Atom>(std::begin(window_properties_),
                                            std::end(window_properties_)));
  }

  connection_->MapWindow({xwindow_});
  window_mapped_in_client_ = true;

  // TODO(thomasanderson): Find out why this flush is necessary.
  connection_->Flush();
}

void X11Window::SetFullscreen(bool fullscreen) {
  SetWMSpecState(fullscreen, x11::GetAtom("_NET_WM_STATE_FULLSCREEN"),
                 x11::Atom::None);
}

bool X11Window::IsActive() const {
  // Focus and stacking order are independent in X11.  Since we cannot guarantee
  // a window is topmost iff it has focus, just use the focus state to determine
  // if a window is active.  Note that Activate() and Deactivate() change the
  // stacking order in addition to changing the focus state.
  return (has_window_focus_ || has_pointer_focus_) && !ignore_keyboard_input_;
}

bool X11Window::IsMinimized() const {
  return HasWMSpecProperty(window_properties_,
                           x11::GetAtom("_NET_WM_STATE_HIDDEN"));
}

bool X11Window::IsMaximized() const {
  return (HasWMSpecProperty(window_properties_,
                            x11::GetAtom("_NET_WM_STATE_MAXIMIZED_VERT")) &&
          HasWMSpecProperty(window_properties_,
                            x11::GetAtom("_NET_WM_STATE_MAXIMIZED_HORZ")));
}

bool X11Window::IsFullscreen() const {
  return HasWMSpecProperty(window_properties_,
                           x11::GetAtom("_NET_WM_STATE_FULLSCREEN"));
}

gfx::Rect X11Window::GetOuterBounds() const {
  gfx::Rect outer_bounds(bounds_in_pixels_);
  outer_bounds.Inset(-native_window_frame_borders_in_pixels_);
  return outer_bounds;
}

void X11Window::ResetWindowRegion() {
  std::unique_ptr<std::vector<x11::Rectangle>> xregion;
  if (!custom_window_shape_ && !IsMaximized() && !IsFullscreen()) {
    SkPath window_mask = GetWindowMaskForXWindow();
    // Some frame views define a custom (non-rectangular) window mask. If
    // so, use it to define the window shape. If not, fall through.
    if (window_mask.countPoints() > 0)
      xregion = x11::CreateRegionFromSkPath(window_mask);
  }
  UpdateWindowRegion(std::move(xregion));
}

void X11Window::OnWorkspaceUpdated() {
  auto old_workspace = workspace_;
  int workspace;
  if (GetWindowDesktop(xwindow_, &workspace))
    workspace_ = workspace;
  else
    workspace_ = base::nullopt;

  if (workspace_ != old_workspace)
    OnXWindowWorkspaceChanged();
}

void X11Window::SetFlashFrameHint(bool flash_frame) {
  if (urgency_hint_set_ == flash_frame)
    return;

  WmHints hints;
  memset(&hints, 0, sizeof(hints));
  GetWmHints(xwindow_, &hints);

  if (flash_frame)
    hints.flags |= WM_HINT_X_URGENCY;
  else
    hints.flags &= ~WM_HINT_X_URGENCY;

  SetWmHints(xwindow_, hints);

  urgency_hint_set_ = flash_frame;
}

void X11Window::UpdateMinAndMaxSize() {
  base::Optional<gfx::Size> minimum_in_pixels = GetMinimumSizeForXWindow();
  base::Optional<gfx::Size> maximum_in_pixels = GetMaximumSizeForXWindow();
  if ((!minimum_in_pixels ||
       min_size_in_pixels_ == minimum_in_pixels.value()) &&
      (!maximum_in_pixels || max_size_in_pixels_ == maximum_in_pixels.value()))
    return;

  min_size_in_pixels_ = minimum_in_pixels.value();
  max_size_in_pixels_ = maximum_in_pixels.value();

  SizeHints hints;
  memset(&hints, 0, sizeof(hints));
  GetWmNormalHints(xwindow_, &hints);

  if (min_size_in_pixels_.IsEmpty()) {
    hints.flags &= ~SIZE_HINT_P_MIN_SIZE;
  } else {
    hints.flags |= SIZE_HINT_P_MIN_SIZE;
    hints.min_width = min_size_in_pixels_.width();
    hints.min_height = min_size_in_pixels_.height();
  }

  if (max_size_in_pixels_.IsEmpty()) {
    hints.flags &= ~SIZE_HINT_P_MAX_SIZE;
  } else {
    hints.flags |= SIZE_HINT_P_MAX_SIZE;
    hints.max_width = max_size_in_pixels_.width();
    hints.max_height = max_size_in_pixels_.height();
  }

  SetWmNormalHints(xwindow_, hints);
}

void X11Window::BeforeActivationStateChanged() {
  was_active_ = IsActive();
  had_pointer_ = has_pointer_;
  had_pointer_grab_ = has_pointer_grab_;
  had_window_focus_ = has_window_focus_;
}

void X11Window::AfterActivationStateChanged() {
  if (had_pointer_grab_ && !has_pointer_grab_)
    OnXWindowLostPointerGrab();

  bool had_pointer_capture = had_pointer_ || had_pointer_grab_;
  bool has_pointer_capture = has_pointer_ || has_pointer_grab_;
  if (had_pointer_capture && !has_pointer_capture)
    OnXWindowLostCapture();

  bool is_active = IsActive();
  if (!was_active_ && is_active)
    SetFlashFrameHint(false);

  if (was_active_ != is_active)
    OnXWindowIsActiveChanged(is_active);
}

void X11Window::OnCrossingEvent(bool enter,
                                bool focus_in_window_or_ancestor,
                                x11::NotifyMode mode,
                                x11::NotifyDetail detail) {
  // NotifyInferior on a crossing event means the pointer moved into or out of a
  // child window, but the pointer is still within |xwindow_|.
  if (detail == x11::NotifyDetail::Inferior)
    return;

  BeforeActivationStateChanged();

  if (mode == x11::NotifyMode::Grab)
    has_pointer_grab_ = enter;
  else if (mode == x11::NotifyMode::Ungrab)
    has_pointer_grab_ = false;

  has_pointer_ = enter;
  if (focus_in_window_or_ancestor && !has_window_focus_) {
    // If we reach this point, we know the focus is in an ancestor or the
    // pointer root.  The definition of |has_pointer_focus_| is (An ancestor
    // window or the PointerRoot is focused) && |has_pointer_|.  Therefore, we
    // can just use |has_pointer_| in the assignment.  The transitions for when
    // the focus changes are handled in OnFocusEvent().
    has_pointer_focus_ = has_pointer_;
  }

  AfterActivationStateChanged();
}

void X11Window::OnFocusEvent(bool focus_in,
                             x11::NotifyMode mode,
                             x11::NotifyDetail detail) {
  // NotifyInferior on a focus event means the focus moved into or out of a
  // child window, but the focus is still within |xwindow_|.
  if (detail == x11::NotifyDetail::Inferior)
    return;

  bool notify_grab =
      mode == x11::NotifyMode::Grab || mode == x11::NotifyMode::Ungrab;

  BeforeActivationStateChanged();

  // For every focus change, the X server sends normal focus events which are
  // useful for tracking |has_window_focus_|, but supplements these events with
  // NotifyPointer events which are only useful for tracking pointer focus.

  // For |has_pointer_focus_| and |has_window_focus_|, we continue tracking
  // state during a grab, but ignore grab/ungrab events themselves.
  if (!notify_grab && detail != x11::NotifyDetail::Pointer)
    has_window_focus_ = focus_in;

  if (!notify_grab && has_pointer_) {
    switch (detail) {
      case x11::NotifyDetail::Ancestor:
      case x11::NotifyDetail::Virtual:
        // If we reach this point, we know |has_pointer_| was true before and
        // after this event.  Since the definition of |has_pointer_focus_| is
        // (An ancestor window or the PointerRoot is focused) && |has_pointer_|,
        // we only need to worry about transitions on the first conjunct.
        // Therefore, |has_pointer_focus_| will become true when:
        // 1. Focus moves from |xwindow_| to an ancestor
        //    (FocusOut with NotifyAncestor)
        // 2. Focus moves from a descendant of |xwindow_| to an ancestor
        //    (FocusOut with NotifyVirtual)
        // |has_pointer_focus_| will become false when:
        // 1. Focus moves from an ancestor to |xwindow_|
        //    (FocusIn with NotifyAncestor)
        // 2. Focus moves from an ancestor to a child of |xwindow_|
        //    (FocusIn with NotifyVirtual)
        has_pointer_focus_ = !focus_in;
        break;
      case x11::NotifyDetail::Pointer:
        // The remaining cases for |has_pointer_focus_| becoming true are:
        // 3. Focus moves from |xwindow_| to the PointerRoot
        // 4. Focus moves from a descendant of |xwindow_| to the PointerRoot
        // 5. Focus moves from None to the PointerRoot
        // 6. Focus moves from Other to the PointerRoot
        // 7. Focus moves from None to an ancestor of |xwindow_|
        // 8. Focus moves from Other to an ancestor of |xwindow_|
        // In each case, we will get a FocusIn with a detail of NotifyPointer.
        // The remaining cases for |has_pointer_focus_| becoming false are:
        // 3. Focus moves from the PointerRoot to |xwindow_|
        // 4. Focus moves from the PointerRoot to a descendant of |xwindow|
        // 5. Focus moves from the PointerRoot to None
        // 6. Focus moves from an ancestor of |xwindow_| to None
        // 7. Focus moves from the PointerRoot to Other
        // 8. Focus moves from an ancestor of |xwindow_| to Other
        // In each case, we will get a FocusOut with a detail of NotifyPointer.
        has_pointer_focus_ = focus_in;
        break;
      case x11::NotifyDetail::Nonlinear:
      case x11::NotifyDetail::NonlinearVirtual:
        // We get Nonlinear(Virtual) events when
        // 1. Focus moves from Other to |xwindow_|
        //    (FocusIn with NotifyNonlinear)
        // 2. Focus moves from Other to a descendant of |xwindow_|
        //    (FocusIn with NotifyNonlinearVirtual)
        // 3. Focus moves from |xwindow_| to Other
        //    (FocusOut with NotifyNonlinear)
        // 4. Focus moves from a descendant of |xwindow_| to Other
        //    (FocusOut with NotifyNonlinearVirtual)
        // |has_pointer_focus_| should be false before and after this event.
        has_pointer_focus_ = false;
        break;
      default:
        break;
    }
  }

  ignore_keyboard_input_ = false;

  AfterActivationStateChanged();
}

bool X11Window::IsTargetedBy(const x11::Event& x11_event) const {
  return x11_event.window() == xwindow_;
}

void X11Window::SetTransientWindow(x11::Window window) {
  transient_window_ = window;
}

void X11Window::HandleEvent(const x11::Event& xev) {
  if (!IsTargetedBy(xev))
    return;

  // We can lose track of the window's position when the window is reparented.
  // When the parent window is moved, we won't get an event, so the window's
  // position relative to the root window will get out-of-sync.  We can re-sync
  // when getting pointer events (EnterNotify, LeaveNotify, ButtonPress,
  // ButtonRelease, MotionNotify) which include the pointer location both
  // relative to this window and relative to the root window, so we can
  // calculate this window's position from that information.
  gfx::Point window_point = EventLocationFromXEvent(xev);
  gfx::Point root_point = EventSystemLocationFromXEvent(xev);
  if (!window_point.IsOrigin() && !root_point.IsOrigin()) {
    gfx::Point window_origin = gfx::Point() + (root_point - window_point);
    if (bounds_in_pixels_.origin() != window_origin) {
      bounds_in_pixels_.set_origin(window_origin);
      NotifyBoundsChanged(bounds_in_pixels_);
    }
  }

  // May want to factor CheckXEventForConsistency(xev); into a common location
  // since it is called here.
  if (auto* crossing = xev.As<x11::CrossingEvent>()) {
    bool focus = crossing->same_screen_focus & CROSSING_FLAG_FOCUS;
    OnCrossingEvent(crossing->opcode == x11::CrossingEvent::EnterNotify, focus,
                    crossing->mode, crossing->detail);
  } else if (auto* expose = xev.As<x11::ExposeEvent>()) {
    gfx::Rect damage_rect_in_pixels(expose->x, expose->y, expose->width,
                                    expose->height);
    OnXWindowDamageEvent(damage_rect_in_pixels);
  } else if (auto* focus = xev.As<x11::FocusEvent>()) {
    OnFocusEvent(focus->opcode == x11::FocusEvent::In, focus->mode,
                 focus->detail);
  } else if (auto* configure = xev.As<x11::ConfigureNotifyEvent>()) {
    OnConfigureEvent(*configure);
  } else if (auto* crossing = xev.As<x11::Input::CrossingEvent>()) {
    TouchFactory* factory = TouchFactory::GetInstance();
    if (factory->ShouldProcessCrossingEvent(*crossing)) {
      auto mode = XI2ModeToXMode(crossing->mode);
      auto detail = XI2DetailToXDetail(crossing->detail);
      switch (crossing->opcode) {
        case x11::Input::CrossingEvent::Enter:
          OnCrossingEvent(true, crossing->focus, mode, detail);
          break;
        case x11::Input::CrossingEvent::Leave:
          OnCrossingEvent(false, crossing->focus, mode, detail);
          break;
        case x11::Input::CrossingEvent::FocusIn:
          OnFocusEvent(true, mode, detail);
          break;
        case x11::Input::CrossingEvent::FocusOut:
          OnFocusEvent(false, mode, detail);
          break;
      }
    }
  } else if (xev.As<x11::MapNotifyEvent>()) {
    OnWindowMapped();
  } else if (xev.As<x11::UnmapNotifyEvent>()) {
    window_mapped_in_server_ = false;
    has_pointer_ = false;
    has_pointer_grab_ = false;
    has_pointer_focus_ = false;
    has_window_focus_ = false;
  } else if (auto* client = xev.As<x11::ClientMessageEvent>()) {
    x11::Atom message_type = client->type;
    if (message_type == x11::GetAtom("WM_PROTOCOLS")) {
      x11::Atom protocol = static_cast<x11::Atom>(client->data.data32[0]);
      if (protocol == x11::GetAtom("WM_DELETE_WINDOW")) {
        // We have received a close message from the window manager.
        OnXWindowCloseRequested();
      } else if (protocol == x11::GetAtom("_NET_WM_PING")) {
        x11::ClientMessageEvent reply_event = *client;
        reply_event.window = x_root_window_;
        x11::SendEvent(reply_event, x_root_window_,
                       x11::EventMask::SubstructureNotify |
                           x11::EventMask::SubstructureRedirect);
      } else if (protocol == x11::GetAtom("_NET_WM_SYNC_REQUEST")) {
        pending_counter_value_ =
            client->data.data32[2] +
            (static_cast<int64_t>(client->data.data32[3]) << 32);
        pending_counter_value_is_extended_ = client->data.data32[4] != 0;
      }
    } else {
      OnXWindowDragDropEvent(*client);
    }
  } else if (auto* property = xev.As<x11::PropertyNotifyEvent>()) {
    x11::Atom changed_atom = property->atom;
    if (changed_atom == x11::GetAtom("_NET_WM_STATE"))
      OnWMStateUpdated();
    else if (changed_atom == x11::GetAtom("_NET_FRAME_EXTENTS"))
      OnFrameExtentsUpdated();
    else if (changed_atom == x11::GetAtom("_NET_WM_DESKTOP"))
      OnWorkspaceUpdated();
  } else if (auto* selection = xev.As<x11::SelectionNotifyEvent>()) {
    OnXWindowSelectionEvent(*selection);
  }
}

void X11Window::UpdateWMUserTime(Event* event) {
  if (!IsActive())
    return;
  DCHECK(event);
  EventType type = event->type();
  if (type == ET_MOUSE_PRESSED || type == ET_KEY_PRESSED ||
      type == ET_TOUCH_PRESSED) {
    uint32_t wm_user_time_ms =
        (event->time_stamp() - base::TimeTicks()).InMilliseconds();
    x11::SetProperty(xwindow_, x11::GetAtom("_NET_WM_USER_TIME"),
                     x11::Atom::CARDINAL, wm_user_time_ms);
  }
}

void X11Window::OnWindowMapped() {
  window_mapped_in_server_ = true;
  // Some WMs only respect maximize hints after the window has been mapped.
  // Check whether we need to re-do a maximization.
  if (should_maximize_after_map_) {
    Maximize();
    should_maximize_after_map_ = false;
  }
}

void X11Window::OnConfigureEvent(const x11::ConfigureNotifyEvent& configure) {
  DCHECK_EQ(xwindow_, configure.window);
  DCHECK_EQ(xwindow_, configure.event);

  if (pending_counter_value_) {
    DCHECK(!configure_counter_value_);
    configure_counter_value_ = pending_counter_value_;
    configure_counter_value_is_extended_ = pending_counter_value_is_extended_;
    pending_counter_value_is_extended_ = false;
    pending_counter_value_ = 0;
  }

  // It's possible that the X window may be resized by some other means than
  // from within aura (e.g. the X window manager can change the size). Make
  // sure the root window size is maintained properly.
  int translated_x_in_pixels = configure.x;
  int translated_y_in_pixels = configure.y;
  if (!configure.send_event && !configure.override_redirect) {
    auto future =
        connection_->TranslateCoordinates({xwindow_, x_root_window_, 0, 0});
    if (auto coords = future.Sync()) {
      translated_x_in_pixels = coords->dst_x;
      translated_y_in_pixels = coords->dst_y;
    }
  }
  gfx::Rect new_bounds_px(translated_x_in_pixels, translated_y_in_pixels,
                          configure.width, configure.height);
  const bool size_changed = bounds_in_pixels_.size() != new_bounds_px.size();
  const bool origin_changed =
      bounds_in_pixels_.origin() != new_bounds_px.origin();
  previous_bounds_in_pixels_ = bounds_in_pixels_;
  bounds_in_pixels_ = new_bounds_px;

  if (size_changed)
    DispatchResize();
  else if (origin_changed)
    NotifyBoundsChanged(bounds_in_pixels_);
}

void X11Window::SetWMSpecState(bool enabled,
                               x11::Atom state1,
                               x11::Atom state2) {
  if (window_mapped_in_client_) {
    ui::SetWMSpecState(xwindow_, enabled, state1, state2);
  } else {
    // The updated state will be set when the window is (re)mapped.
    base::flat_set<x11::Atom> new_window_properties = window_properties_;
    for (x11::Atom atom : {state1, state2}) {
      if (enabled)
        new_window_properties.insert(atom);
      else
        new_window_properties.erase(atom);
    }
    UpdateWindowProperties(new_window_properties);
  }
}

void X11Window::OnWMStateUpdated() {
  // The EWMH spec requires window managers to remove the _NET_WM_STATE property
  // when a window is unmapped.  However, Chromium code wants the state to
  // persist across a Hide() and Show().  So if the window is currently
  // unmapped, leave the state unchanged so it will be restored when the window
  // is remapped.
  std::vector<x11::Atom> atom_list;
  if (GetArrayProperty(xwindow_, x11::GetAtom("_NET_WM_STATE"), &atom_list) ||
      window_mapped_in_client_) {
    UpdateWindowProperties(
        base::flat_set<x11::Atom>(std::begin(atom_list), std::end(atom_list)));
  }
}

void X11Window::UpdateWindowProperties(
    const base::flat_set<x11::Atom>& new_window_properties) {
  was_minimized_ = IsMinimized();

  window_properties_ = new_window_properties;

  // Ignore requests by the window manager to enter or exit fullscreen (e.g. as
  // a result of pressing a window manager accelerator key). Chrome does not
  // handle window manager initiated fullscreen. In particular, Chrome needs to
  // do preprocessing before the x window's fullscreen state is toggled.

  is_always_on_top_ = HasWMSpecProperty(window_properties_,
                                        x11::GetAtom("_NET_WM_STATE_ABOVE"));
  OnXWindowStateChanged();
  ResetWindowRegion();
}

void X11Window::OnFrameExtentsUpdated() {
  std::vector<int32_t> insets;
  if (GetArrayProperty(xwindow_, x11::GetAtom("_NET_FRAME_EXTENTS"), &insets) &&
      insets.size() == 4) {
    // |insets| are returned in the order: [left, right, top, bottom].
    native_window_frame_borders_in_pixels_ =
        gfx::Insets(insets[2], insets[0], insets[3], insets[1]);
  } else {
    native_window_frame_borders_in_pixels_ = gfx::Insets();
  }
}

// Removes |delayed_resize_task_| from the task queue (if it's in the queue) and
// adds it back at the end of the queue.
void X11Window::DispatchResize() {
  if (update_counter_ == x11::Sync::Counter{} ||
      configure_counter_value_ == 0) {
    // WM doesn't support _NET_WM_SYNC_REQUEST. Or we are too slow, so
    // _NET_WM_SYNC_REQUEST is disabled by the compositor.
    delayed_resize_task_.Reset(base::BindOnce(
        &X11Window::DelayedResize, base::Unretained(this), bounds_in_pixels_));
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, delayed_resize_task_.callback());
    return;
  }

  if (configure_counter_value_is_extended_) {
    current_counter_value_ = configure_counter_value_;
    configure_counter_value_ = 0;
    // Make sure the counter is even number.
    if ((current_counter_value_ % 2) == 1)
      ++current_counter_value_;
  }

  // If _NET_WM_SYNC_REQUEST is used to synchronize with compositor during
  // resizing, the compositor will not resize the window, until last resize is
  // handled, so we don't need accumulate resize events.
  DelayedResize(bounds_in_pixels_);
}

void X11Window::DelayedResize(const gfx::Rect& bounds_in_pixels) {
  if (configure_counter_value_is_extended_ &&
      (current_counter_value_ % 2) == 0) {
    // Increase the |extended_update_counter_|, so the compositor will know we
    // are not frozen and re-enable _NET_WM_SYNC_REQUEST, if it was disabled.
    // Increase the |extended_update_counter_| to an odd number will not trigger
    // a new resize.
    SyncSetCounter(connection_, extended_update_counter_,
                   ++current_counter_value_);
  }

  CancelResize();
  NotifyBoundsChanged(bounds_in_pixels);

  // No more member accesses here: bounds change propagation may have deleted
  // |this| (e.g. when a chrome window is snapped into a tab strip. Further
  // details at crbug.com/1068755).
}

void X11Window::CancelResize() {
  delayed_resize_task_.Cancel();
}

void X11Window::UnconfineCursor() {
  if (!has_pointer_barriers_)
    return;

  for (auto pointer_barrier : pointer_barriers_)
    connection_->xfixes().DeletePointerBarrier({pointer_barrier});

  pointer_barriers_.fill({});

  has_pointer_barriers_ = false;
}

void X11Window::UpdateWindowRegion(
    std::unique_ptr<std::vector<x11::Rectangle>> region) {
  auto set_shape = [&](const std::vector<x11::Rectangle>& rectangles) {
    connection_->shape().Rectangles(x11::Shape::RectanglesRequest{
        .operation = x11::Shape::So::Set,
        .destination_kind = x11::Shape::Sk::Bounding,
        .ordering = x11::ClipOrdering::YXBanded,
        .destination_window = xwindow_,
        .rectangles = rectangles,
    });
  };

  // If a custom window shape was supplied then apply it.
  if (custom_window_shape_) {
    set_shape(*window_shape_);
    return;
  }

  window_shape_ = std::move(region);
  if (window_shape_) {
    set_shape(*window_shape_);
    return;
  }

  // If we didn't set the shape for any reason, reset the shaping information.
  // How this is done depends on the border style, due to quirks and bugs in
  // various window managers.
  if (use_native_frame_) {
    // If the window has system borders, the mask must be set to null (not a
    // rectangle), because several window managers (eg, KDE, XFCE, XMonad) will
    // not put borders on a window with a custom shape.
    connection_->shape().Mask(x11::Shape::MaskRequest{
        .operation = x11::Shape::So::Set,
        .destination_kind = x11::Shape::Sk::Bounding,
        .destination_window = xwindow_,
        .source_bitmap = x11::Pixmap::None,
    });
  } else {
    // Conversely, if the window does not have system borders, the mask must be
    // manually set to a rectangle that covers the whole window (not null). This
    // is due to a bug in KWin <= 4.11.5 (KDE bug #330573) where setting a null
    // shape causes the hint to disable system borders to be ignored (resulting
    // in a double border).
    x11::Rectangle r{0, 0, bounds_in_pixels_.width(),
                     bounds_in_pixels_.height()};
    set_shape({r});
  }
}

void X11Window::NotifyBoundsChanged(const gfx::Rect& new_bounds_in_px) {
  ResetWindowRegion();
  OnXWindowBoundsChanged(new_bounds_in_px);
}

bool X11Window::InitializeAsStatusIcon() {
  std::string atom_name = "_NET_SYSTEM_TRAY_S" +
                          base::NumberToString(connection_->DefaultScreenId());
  auto reply = connection_->GetSelectionOwner({x11::GetAtom(atom_name)}).Sync();
  if (!reply || reply->owner == x11::Window::None)
    return false;
  auto manager = reply->owner;

  SetArrayProperty(
      xwindow_, x11::GetAtom("_XEMBED_INFO"), x11::Atom::CARDINAL,
      std::vector<uint32_t>{kXembedInfoProtocolVersion, kXembedInfoFlags});

  x11::ChangeWindowAttributesRequest req{xwindow_};
  if (visual_has_alpha_) {
    req.background_pixel = 0;
  } else {
    x11::SetProperty(xwindow_, x11::GetAtom("CHROMIUM_COMPOSITE_WINDOW"),
                     x11::Atom::CARDINAL, static_cast<uint32_t>(1));
    req.background_pixmap =
        static_cast<x11::Pixmap>(x11::BackPixmap::ParentRelative);
  }
  connection_->ChangeWindowAttributes(req);

  auto future = SendClientMessage(
      manager, manager, x11::GetAtom("_NET_SYSTEM_TRAY_OPCODE"),
      {static_cast<uint32_t>(X11EventSource::GetInstance()->GetTimestamp()),
       kSystemTrayRequestDock, static_cast<uint32_t>(xwindow_), 0, 0},
      x11::EventMask::NoEvent);
  return !future.Sync().error;
}

}  // namespace ui
