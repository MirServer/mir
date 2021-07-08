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

#include "wl_keyboard.h"

#include "wayland_utils.h"
#include "wl_surface.h"
#include "wl_seat.h"

#include "mir/executor.h"
#include "mir/anonymous_shm_file.h"
#include "mir/input/keymap.h"
#include "mir/input/xkb_mapper.h"
#include "mir/log.h"
#include "mir/fatal.h"

#include <xkbcommon/xkbcommon.h>
#include <boost/throw_exception.hpp>

#include <cstring> // memcpy

namespace mf = mir::frontend;
namespace mw = mir::wayland;
namespace mi = mir::input;

mf::WlKeyboard::WlKeyboard(
    wl_resource* new_resource,
    std::shared_ptr<mi::Keymap> const& initial_keymap,
    std::function<std::vector<uint32_t>()> const& acquire_current_keyboard_state,
    bool enable_key_repeat)
    : Keyboard(new_resource, Version<6>()),
      current_keymap{nullptr}, // will be set later in the constructor by set_keymap()
      compiled_keymap{nullptr, &xkb_keymap_unref},
      state{nullptr, &xkb_state_unref},
      context{xkb_context_new(XKB_CONTEXT_NO_FLAGS), &xkb_context_unref},
      acquire_current_keyboard_state{acquire_current_keyboard_state}
{
    // TODO: We should really grab the keymap for the focused surface when
    // we receive focus.

    // TODO: Maintain per-device keymaps, and send the appropriate map before
    // sending an event from a keyboard with a different map.

    /* The wayland::Keyboard constructor has already run, creating the keyboard
     * resource. It is thus safe to send a keymap event to it; the client will receive
     * the keyboard object before this event.
     */
    set_keymap(initial_keymap);

    // 25 rate and 600 delay are the default in Weston and Sway
    // At some point we will want to make this configurable
    if (version_supports_repeat_info())
        send_repeat_info_event(enable_key_repeat? 25 : 0, 600);
}

mf::WlKeyboard::~WlKeyboard()
{
    if (focused_surface)
    {
        focused_surface.value().remove_destroy_listener(destroy_listener_id);
    }
}

void mf::WlKeyboard::event(MirKeyboardEvent const* event, WlSurface& surface)
{
    bool down;
    switch (mir_keyboard_event_action(event))
    {
    case mir_keyboard_action_down:
        down = true;
        break;

    case mir_keyboard_action_up:
        down = false;
        break;

    default:
        return;
    }

    auto const serial = wl_display_next_serial(wl_client_get_display(client));
    auto const timestamp = mir_input_event_get_wayland_timestamp(mir_keyboard_event_input_event(event));
    int const scancode = mir_keyboard_event_scan_code(event);
    uint32_t const wayland_state = down ? KeyState::pressed : KeyState::released;
    send_key_event(serial, timestamp, scancode, wayland_state);

    if (as_nullable_ptr(focused_surface) == &surface)
    {
        /*
         * HACK! Maintain our own XKB state, so we can serialise it for
         * wl_keyboard_send_modifiers
         */
        xkb_key_direction const xkb_state = down ? XKB_KEY_DOWN : XKB_KEY_UP;
        xkb_state_update_key(state.get(), scancode + 8, xkb_state);

        update_modifier_state();
    }
    else
    {
        log_warning(
            "Sending key 0x%2.2x %s to wl_surface@%u even though it was not explicitly given keyboard focus",
            scancode, down ? "press" : "release", wl_resource_get_id(surface.resource));

        focussed(surface, true);
    }
}

void mf::WlKeyboard::focussed(WlSurface& surface, bool should_be_focused)
{
    if (client != surface.client)
    {
        fatal_error("WlKeyboard::focussed() called with a surface of the wrong client");
    }

    bool const is_currently_focused = (focused_surface && &focused_surface.value() == &surface);

    if (should_be_focused == is_currently_focused)
        return;

    if (focused_surface)
    {
        focused_surface.value().remove_destroy_listener(destroy_listener_id);
        auto const serial = wl_display_next_serial(wl_client_get_display(client));
        send_leave_event(serial, focused_surface.value().raw_resource());
    }

    if (should_be_focused)
    {
        // TODO: Send the surface's keymap here

        auto const keyboard_state = acquire_current_keyboard_state();
        update_keyboard_state(keyboard_state);

        wl_array key_state;
        wl_array_init(&key_state);

        auto* const array_storage = wl_array_add(
            &key_state,
            keyboard_state.size() * sizeof(decltype(keyboard_state)::value_type));

        if (!array_storage)
        {
            wl_resource_post_no_memory(resource);
            BOOST_THROW_EXCEPTION(std::bad_alloc());
        }

        if (!keyboard_state.empty())
        {
            std::memcpy(
                array_storage,
                keyboard_state.data(),
                keyboard_state.size() * sizeof(decltype(keyboard_state)::value_type));
        }

        destroy_listener_id = surface.add_destroy_listener(
            [this, &surface]()
            {
                focussed(surface, false);
            });

        auto const serial = wl_display_next_serial(wl_client_get_display(client));
        send_enter_event(serial, surface.raw_resource(), &key_state);
        wl_array_release(&key_state);

        focused_surface = mw::make_weak(&surface);
    }
    else
    {
        destroy_listener_id = {};
        focused_surface = {};
    }
}

void mf::WlKeyboard::update_keyboard_state(std::vector<uint32_t> const& keyboard_state)
{
    // Rebuild xkb state
    state = decltype(state)(xkb_state_new(compiled_keymap.get()), &xkb_state_unref);
    for (auto scancode : keyboard_state)
    {
        xkb_state_update_key(state.get(), scancode + 8, XKB_KEY_DOWN);
    }

    update_modifier_state();
}

void mf::WlKeyboard::set_keymap(std::shared_ptr<mi::Keymap> const& new_keymap)
{
    if (!new_keymap || new_keymap == current_keymap)
    {
        return;
    }

    if (current_keymap && current_keymap->matches(*new_keymap))
    {
        current_keymap = new_keymap;
        return;
    }

    current_keymap = new_keymap;
    compiled_keymap = new_keymap->make_unique_xkb_keymap(context.get());

    // TODO: We might need to copy across the existing depressed keys?
    state = decltype(state)(xkb_state_new(compiled_keymap.get()), &xkb_state_unref);

    std::unique_ptr<char, void(*)(void*)> buffer{xkb_keymap_get_as_string(
        compiled_keymap.get(),
        XKB_KEYMAP_FORMAT_TEXT_V1),
        free};
    auto length = strlen(buffer.get());

    mir::AnonymousShmFile shm_buffer{length};
    memcpy(shm_buffer.base_ptr(), buffer.get(), length);

    send_keymap_event(KeymapFormat::xkb_v1,
                      Fd{IntOwnedFd{shm_buffer.fd()}},
                      length);
}

void mf::WlKeyboard::update_modifier_state()
{
    // TODO?
    // assert_on_wayland_event_loop()

    auto new_depressed_mods = xkb_state_serialize_mods(
        state.get(),
        XKB_STATE_MODS_DEPRESSED);
    auto new_latched_mods = xkb_state_serialize_mods(
        state.get(),
        XKB_STATE_MODS_LATCHED);
    auto new_locked_mods = xkb_state_serialize_mods(
        state.get(),
        XKB_STATE_MODS_LOCKED);
    auto new_group = xkb_state_serialize_layout(
        state.get(),
        XKB_STATE_LAYOUT_EFFECTIVE);

    if ((new_depressed_mods != mods_depressed) ||
        (new_latched_mods != mods_latched) ||
        (new_locked_mods != mods_locked) ||
        (new_group != group))
    {
        mods_depressed = new_depressed_mods;
        mods_latched = new_latched_mods;
        mods_locked = new_locked_mods;
        group = new_group;

        send_modifiers_event(wl_display_get_serial(wl_client_get_display(client)),
                             mods_depressed,
                             mods_latched,
                             mods_locked,
                             group);
    }
}

void mir::frontend::WlKeyboard::resync_keyboard()
{
    update_keyboard_state(acquire_current_keyboard_state());
}
