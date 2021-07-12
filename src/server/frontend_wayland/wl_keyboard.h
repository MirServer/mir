/*
 * Copyright © 2018-2019 Canonical Ltd.
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
 * Authored by: Christopher James Halse Rogers <christopher.halse.rogers@canonical.com>
 */

#ifndef MIR_FRONTEND_WL_KEYBOARD_H
#define MIR_FRONTEND_WL_KEYBOARD_H

#include "wayland_wrapper.h"

#include <vector>
#include <functional>
#include <chrono>

struct MirKeyboardEvent;

// from <xkbcommon/xkbcommon.h>
struct xkb_keymap;
struct xkb_state;
struct xkb_context;

namespace mir
{

class Executor;

namespace input
{
class Keymap;
}

namespace frontend
{
class WlSurface;

class WlKeyboard : public wayland::Keyboard
{
public:
    WlKeyboard(
        wl_resource* new_resource,
        std::shared_ptr<mir::input::Keymap> const& initial_keymap,
        std::function<std::vector<uint32_t>()> const& acquire_current_keyboard_state,
        bool enable_key_repeat);

    ~WlKeyboard();

    void event(MirKeyboardEvent const* event, WlSurface& surface);
    void focussed(WlSurface& surface, bool should_be_focused);
    void resync_keyboard();

private:
    void set_keymap(std::shared_ptr<mir::input::Keymap> const& new_keymap);
    void update_modifier_state();
    void update_keyboard_state(std::vector<uint32_t> const& keyboard_state);

    std::shared_ptr<mir::input::Keymap> current_keymap;
    std::unique_ptr<xkb_keymap, void (*)(xkb_keymap *)> compiled_keymap;
    std::unique_ptr<xkb_state, void (*)(xkb_state *)> state;
    std::unique_ptr<xkb_context, void (*)(xkb_context *)> const context;

    std::function<std::vector<uint32_t>()> const acquire_current_keyboard_state;

    wayland::Weak<WlSurface> focused_surface;
    wayland::DestroyListenerId destroy_listener_id;

    uint32_t mods_depressed{0};
    uint32_t mods_latched{0};
    uint32_t mods_locked{0};
    uint32_t group{0};
};
}
}

#endif // MIR_FRONTEND_WL_KEYBOARD_H
