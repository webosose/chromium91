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

#ifndef UI_OZONE_PLATFORM_WAYLAND_HOST_WAYLAND_SEAT_MANAGER__H_
#define UI_OZONE_PLATFORM_WAYLAND_HOST_WAYLAND_SEAT_MANAGER__H_

#include <cstdint>
#include <memory>
#include <vector>

class SkBitmap;

struct wl_seat;

namespace gfx {
class Point;
}

namespace ui {

class WaylandConnection;
class WaylandSeat;

// Stores WaylandSeat wrapper objects.
class WaylandSeatManager {
 public:
  WaylandSeatManager();
  WaylandSeatManager(const WaylandSeatManager&) = delete;
  WaylandSeatManager& operator=(const WaylandSeatManager&) = delete;
  virtual ~WaylandSeatManager();

  // Adds Wayland |seat| with the |seat_id| to the storage.
  void AddSeat(WaylandConnection* connection,
               std::uint32_t seat_id,
               wl_seat* seat);
  // Returns the very first WaylandSeat wrapper.
  WaylandSeat* GetFirstSeat() const;

  // Updates bitmaps for all cursors within the storage.
  void UpdateCursorBitmap(const std::vector<SkBitmap>& bitmaps,
                          const gfx::Point& hotspot_in_dips,
                          int buffer_scale);

  // Resets keyboard for all seats within the storage.
  void CreateKeyboard();

 private:
  using SeatList = std::vector<std::unique_ptr<WaylandSeat>>;
  SeatList::const_iterator GetSeatItById(std::uint32_t seat_id) const;

  SeatList seat_list_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_WAYLAND_HOST_WAYLAND_SEAT_MANAGER__H_
