/*
 * Copyright © 2021 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: William Wold <william.wold@canonical.com>
 */

#ifndef MIR_FRONTEND_VIRTUAL_KEYBOARD_V1_H
#define MIR_FRONTEND_VIRTUAL_KEYBOARD_V1_H

#include <memory>
struct wl_display;

namespace mir
{
namespace input
{
class InputDeviceHub;
class Seat;
class Keymap;
class InputDeviceRegistry;
}
namespace frontend
{
class VirtualKeyboardManagerV1Global;

auto create_virtual_keyboard_manager_v1(
    wl_display* display,
    std::shared_ptr<input::InputDeviceRegistry> const& device_registry)
-> std::shared_ptr<VirtualKeyboardManagerV1Global>;
}
}

#endif // MIR_FRONTEND_VIRTUAL_KEYBOARD_V1_H
