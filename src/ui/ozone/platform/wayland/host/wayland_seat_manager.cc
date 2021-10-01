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

#include "ui/ozone/platform/wayland/host/wayland_seat_manager.h"

#include <algorithm>

#include "base/logging.h"
#include "ui/ozone/platform/wayland/host/wayland_cursor.h"
#include "ui/ozone/platform/wayland/host/wayland_seat.h"

namespace ui {

WaylandSeatManager::WaylandSeatManager() = default;

WaylandSeatManager::~WaylandSeatManager() = default;

void WaylandSeatManager::AddSeat(WaylandConnection* connection,
                                 std::uint32_t seat_id,
                                 wl_seat* seat) {
  auto seat_it = GetSeatItById(seat_id);
  DCHECK(seat_it == seat_list_.end());

  seat_list_.push_back(
      std::make_unique<WaylandSeat>(connection, seat_id, seat));
}

WaylandSeat* WaylandSeatManager::GetFirstSeat() const {
  if (!seat_list_.empty())
    return seat_list_.front().get();
  return nullptr;
}

void WaylandSeatManager::UpdateCursorBitmap(
    const std::vector<SkBitmap>& bitmaps,
    const gfx::Point& hotspot_in_dips,
    int buffer_scale) {
  for (auto& seat : seat_list_)
    if (seat->cursor())
      seat->cursor()->UpdateBitmap(bitmaps, hotspot_in_dips, buffer_scale);
}

void WaylandSeatManager::CreateKeyboard() {
  for (auto& seat : seat_list_)
    seat->CreateKeyboard();
}

WaylandSeatManager::SeatList::const_iterator WaylandSeatManager::GetSeatItById(
    std::uint32_t seat_id) const {
  return std::find_if(
      seat_list_.begin(), seat_list_.end(),
      [seat_id](const auto& item) { return item->seat_id() == seat_id; });
}

}  // namespace ui
