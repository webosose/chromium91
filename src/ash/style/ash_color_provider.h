// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_STYLE_ASH_COLOR_PROVIDER_H_
#define ASH_STYLE_ASH_COLOR_PROVIDER_H_

#include "ash/ash_export.h"
#include "ash/public/cpp/session/session_observer.h"
#include "ash/public/cpp/style/color_provider.h"
#include "base/observer_list.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/vector_icon_types.h"

class PrefChangeRegistrar;
class PrefRegistrySimple;
class PrefService;

namespace views {
class ImageButton;
class LabelButton;
}  // namespace views

namespace ash {
class ColorModeObserver;

// The color provider for system UI. It provides colors for Shield layer, Base
// layer, Controls layer and Content layer. Shield layer is a combination of
// color, opacity and blur which may change depending on the context, it is
// usually a fullscreen layer. e.g, PowerButtoneMenuScreenView for power button
// menu. Base layer is the bottom layer of any UI displayed on top of all other
// UIs. e.g, the ShelfView that contains all the shelf items. Controls layer is
// where components such as icons and inkdrops lay on, it may also indicate the
// state of an interactive element (active/inactive states). Content layer means
// the UI elements, e.g., separator, text, icon. The color of an element in
// system UI will be the combination of the colors of the four layers.
class ASH_EXPORT AshColorProvider : public SessionObserver,
                                    public ColorProvider {
 public:
  AshColorProvider();
  AshColorProvider(const AshColorProvider& other) = delete;
  AshColorProvider operator=(const AshColorProvider& other) = delete;
  ~AshColorProvider() override;

  static AshColorProvider* Get();

  // Gets the disabled color on |enabled_color|. It can be disabled background,
  // an disabled icon, etc.
  static SkColor GetDisabledColor(SkColor enabled_color);

  // Gets the color of second tone on the given |color_of_first_tone|. e.g,
  // power status icon inside status area is a dual tone icon.
  static SkColor GetSecondToneColor(SkColor color_of_first_tone);

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // SessionObserver:
  void OnActiveUserPrefServiceChanged(PrefService* prefs) override;
  void OnSessionStateChanged(session_manager::SessionState state) override;

  // ColorProvider:
  SkColor GetShieldLayerColor(ShieldLayerType type) const override;
  SkColor GetBaseLayerColor(BaseLayerType type) const override;
  SkColor GetControlsLayerColor(ControlsLayerType type) const override;
  SkColor GetContentLayerColor(ContentLayerType type) const override;
  RippleAttributes GetRippleAttributes(
      SkColor bg_color = gfx::kPlaceholderColor) const override;
  void AddObserver(ColorModeObserver* observer) override;
  void RemoveObserver(ColorModeObserver* observer) override;
  bool IsDarkModeEnabled() const override;

  // Gets the background color that can be applied on any layer. The returned
  // color will be different based on color mode and color theme (see
  // |is_themed_|).
  SkColor GetBackgroundColor() const;

  // Helpers to style different types of buttons. Depending on the type may
  // style text, icon and background colors for both enabled and disabled
  // states. May overwrite an prior styles on |button|.
  void DecoratePillButton(views::LabelButton* button,
                          const gfx::VectorIcon* icon);
  void DecorateCloseButton(views::ImageButton* button,
                           int button_size,
                           const gfx::VectorIcon& icon);
  void DecorateIconButton(views::ImageButton* button,
                          const gfx::VectorIcon& icon,
                          bool toggled,
                          int icon_size);
  void DecorateFloatingIconButton(views::ImageButton* button,
                                  const gfx::VectorIcon& icon);

  // Whether the system color mode is themed, by default is true. If true, the
  // background color will be calculated based on extracted wallpaper color.
  bool IsThemed() const;

  // Toggles pref |kDarkModeEnabled|.
  void ToggleColorMode();

  // Updates pref |kColorModeThemed| to |is_themed|.
  void UpdateColorModeThemed(bool is_themed);

 private:
  friend class ScopedLightModeAsDefault;

  // Gets the background default color.
  SkColor GetBackgroundDefaultColor() const;

  // Gets the background themed color that's calculated based on the color
  // extracted from wallpaper. For dark mode, it will be dark muted wallpaper
  // prominent color + SK_ColorBLACK 50%. For light mode, it will be light
  // muted wallpaper prominent color + SK_ColorWHITE 75%.
  SkColor GetBackgroundThemedColor() const;

  // Notifies all the observers on |kDarkModeEnabled|'s change.
  void NotifyDarkModeEnabledPrefChange();

  // Notifies all the observers on |kColorModeThemed|'s change.
  void NotifyColorModeThemedPrefChange();

  // Default color mode is dark, which is controlled by pref |kDarkModeEnabled|
  // currently. But we can also override it to light through
  // ScopedLightModeAsDefault. This is done to help keeping some of the UI
  // elements as light by default before launching dark/light mode. Overriding
  // only if the kDarkLightMode feature is disabled. This variable will be
  // removed once enabled dark/light mode.
  bool override_light_mode_as_default_ = false;

  base::ObserverList<ColorModeObserver> observers_;
  std::unique_ptr<PrefChangeRegistrar> pref_change_registrar_;
  PrefService* active_user_pref_service_ = nullptr;  // Not owned.
};

}  // namespace ash

#endif  // ASH_STYLE_ASH_COLOR_PROVIDER_H_
