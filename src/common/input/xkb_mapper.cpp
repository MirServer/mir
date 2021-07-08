/*
 * Copyright © 2013-2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by:
 *   Robert Carr <robert.carr@canonical.com>
 *   Andreas Pokorny <andreas.pokorny@canonical.com>
 */

#include "mir/input/xkb_mapper.h"
#include "mir/input/keymap.h"
#include "mir/events/event_private.h"
#include "mir/events/event_builders.h"

#include <boost/throw_exception.hpp>

#include <linux/input-event-codes.h>

#include <sstream>
#include <unordered_set>

namespace mi = mir::input;
namespace mev = mir::events;
namespace mircv = mi::receiver;

namespace
{

char const* get_locale_from_environment()
{
    char const* loc = getenv("LC_ALL");
    if (!loc)
        loc = getenv("LC_CTYPE");
    if (!loc)
        loc = getenv("LANG");
    if (!loc)
        loc = "C";
    return loc;
}

uint32_t constexpr to_xkb_scan_code(uint32_t evdev_scan_code)
{
    // xkb scancodes are offset by 8 from evdev scancodes for compatibility with X protocol.
    return evdev_scan_code + 8;
}

MirInputEventModifiers modifier_from_xkb_scan_code(uint32_t key)
{
    switch(key)
    {
    case to_xkb_scan_code(KEY_RIGHTSHIFT): return mir_input_event_modifier_shift_right;
    case to_xkb_scan_code(KEY_LEFTSHIFT): return mir_input_event_modifier_shift_left;
    case to_xkb_scan_code(KEY_RIGHTALT): return mir_input_event_modifier_alt_right;
    case to_xkb_scan_code(KEY_LEFTALT): return mir_input_event_modifier_alt_left;
    case to_xkb_scan_code(KEY_RIGHTCTRL): return mir_input_event_modifier_ctrl_right;
    case to_xkb_scan_code(KEY_LEFTCTRL): return mir_input_event_modifier_ctrl_left;
    case to_xkb_scan_code(KEY_LEFTMETA): return mir_input_event_modifier_meta_left;
    case to_xkb_scan_code(KEY_RIGHTMETA): return mir_input_event_modifier_meta_right;
    case to_xkb_scan_code(KEY_CAPSLOCK): return mir_input_event_modifier_caps_lock;
    case to_xkb_scan_code(KEY_SCREENLOCK): return mir_input_event_modifier_scroll_lock;
    case to_xkb_scan_code(KEY_NUMLOCK): return mir_input_event_modifier_num_lock;
    default: return MirInputEventModifiers{0};
    }
}

bool is_toggle_modifier(MirInputEventModifiers key)
{
    return key == mir_input_event_modifier_caps_lock ||
        key == mir_input_event_modifier_scroll_lock ||
        key == mir_input_event_modifier_num_lock;
}

MirInputEventModifiers expand_modifiers(MirInputEventModifiers modifiers)
{
    if (modifiers == 0)
        return mir_input_event_modifier_none;

    if ((modifiers & mir_input_event_modifier_alt_left) || (modifiers & mir_input_event_modifier_alt_right))
        modifiers |= mir_input_event_modifier_alt;

    if ((modifiers & mir_input_event_modifier_ctrl_left) || (modifiers & mir_input_event_modifier_ctrl_right))
        modifiers |= mir_input_event_modifier_ctrl;

    if ((modifiers & mir_input_event_modifier_shift_left) || (modifiers & mir_input_event_modifier_shift_right))
        modifiers |= mir_input_event_modifier_shift;

    if ((modifiers & mir_input_event_modifier_meta_left) || (modifiers & mir_input_event_modifier_meta_right))
        modifiers |= mir_input_event_modifier_meta;

    return modifiers;
}

mi::XKBComposeStatePtr make_unique_compose_state(mi::XKBComposeTablePtr const& table)
{
    return {xkb_compose_state_new(table.get(), XKB_COMPOSE_STATE_NO_FLAGS), &xkb_compose_state_unref};
}

mi::XKBComposeTablePtr make_unique_compose_table_from_locale(mi::XKBContextPtr const& context, std::string const& locale)
{
    return {xkb_compose_table_new_from_locale(
            context.get(),
            locale.c_str(),
            XKB_COMPOSE_COMPILE_NO_FLAGS),
           &xkb_compose_table_unref};
}

mi::XKBStatePtr make_unique_state(xkb_keymap* keymap)
{
    return {xkb_state_new(keymap), xkb_state_unref};
}
}

mi::XKBContextPtr mi::make_unique_context()
{
    return {xkb_context_new(xkb_context_flags(0)), &xkb_context_unref};
}

mircv::XKBMapper::XKBMapper() :
    context{make_unique_context()},
    compose_table{make_unique_compose_table_from_locale(context, get_locale_from_environment())}
{
}

void mircv::XKBMapper::set_key_state(MirInputDeviceId id, std::vector<uint32_t> const& key_state)
{
    std::lock_guard<std::mutex> lg(guard);

    auto mapping_state = get_keymapping_state(id);
    if (mapping_state)
        mapping_state->set_key_state(key_state);
}

void mircv::XKBMapper::update_modifier()
{
    modifier_state = mir::optional_value<MirInputEventModifiers>{};
    if (!device_mapping.empty())
    {
        MirInputEventModifiers new_modifier = 0;
        for (auto const& mapping_state : device_mapping)
        {
            new_modifier |= mapping_state.second->modifiers();
        }

        modifier_state = new_modifier;
    }
}

void mircv::XKBMapper::map_event(MirEvent& ev)
{
    std::lock_guard<std::mutex> lg(guard);

    auto type = mir_event_get_type(&ev);

    if (type == mir_event_type_input)
    {
        auto input_event = mir_event_get_input_event(&ev);
        auto input_type = mir_input_event_get_type(input_event);
        auto device_id = mir_input_event_get_device_id(input_event);

        if (input_type == mir_input_event_type_key)
        {
            auto mapping_state = get_keymapping_state(device_id);
            if (mapping_state)
            {
                auto compose_state = get_compose_state(device_id);
                if (mapping_state->update_and_map(ev, compose_state))
                    update_modifier();
            }
        }
        else if (modifier_state.is_set())
        {
            mev::set_modifier(ev, expand_modifiers(modifier_state.value()));
        }
    }
}

mircv::XKBMapper::XkbMappingState* mircv::XKBMapper::get_keymapping_state(MirInputDeviceId id)
{
    auto dev_keymap = device_mapping.find(id);

    if (dev_keymap != end(device_mapping))
    {
        return dev_keymap->second.get();
    }
    if (default_keymap)
    {
        auto mapping_state = std::make_unique<XkbMappingState>(default_keymap, default_compiled_keymap);
        decltype(device_mapping.begin()) insertion_pos;
        std::tie(insertion_pos, std::ignore) =
            device_mapping.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(id),
                std::forward_as_tuple(std::move(mapping_state)));

        return insertion_pos->second.get();
    }
    return nullptr;
}

void mircv::XKBMapper::set_keymap_for_all_devices(std::shared_ptr<Keymap> new_keymap)
{
    set_keymap(new_keymap);
}

void mircv::XKBMapper::set_keymap(std::shared_ptr<Keymap> new_keymap)
{
    std::lock_guard<std::mutex> lg(guard);
    default_keymap = std::move(new_keymap);
    default_compiled_keymap = default_keymap->make_unique_xkb_keymap(context.get());
    device_mapping.clear();
}

void mircv::XKBMapper::set_keymap_for_device(MirInputDeviceId id, std::shared_ptr<Keymap> new_keymap)
{
    set_keymap(id, std::move(new_keymap));
}

void mircv::XKBMapper::set_keymap(MirInputDeviceId id, std::shared_ptr<Keymap> new_keymap)
{
    std::lock_guard<std::mutex> lg(guard);

    auto compiled_keymap = new_keymap->make_unique_xkb_keymap(context.get());
    auto mapping_state = std::make_unique<XkbMappingState>(std::move(new_keymap), std::move(compiled_keymap));

    device_mapping.erase(id);
    device_mapping.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(id),
        std::forward_as_tuple(std::move(mapping_state)));
}

void mircv::XKBMapper::clear_all_keymaps()
{
    std::lock_guard<std::mutex> lg(guard);
    default_keymap.reset();
    device_mapping.clear();
    update_modifier();
}

void mircv::XKBMapper::clear_keymap_for_device(MirInputDeviceId id)
{
    std::lock_guard<std::mutex> lg(guard);
    device_mapping.erase(id);
    update_modifier();
}

MirInputEventModifiers mircv::XKBMapper::modifiers() const
{
    std::lock_guard<std::mutex> lg(guard);
    if (modifier_state.is_set())
        return expand_modifiers(modifier_state.value());
    return mir_input_event_modifier_none;
}

MirInputEventModifiers mircv::XKBMapper::device_modifiers(MirInputDeviceId id) const
{
    std::lock_guard<std::mutex> lg(guard);

    auto it = device_mapping.find(id);
    if (it == end(device_mapping))
        return mir_input_event_modifier_none;
    return expand_modifiers(it->second->modifiers());
}

mircv::XKBMapper::XkbMappingState::XkbMappingState(
    std::shared_ptr<Keymap> keymap,
    std::shared_ptr<xkb_keymap> compiled_keymap)
    : keymap{std::move(keymap)},
      compiled_keymap{std::move(compiled_keymap)},
      state{make_unique_state(this->compiled_keymap.get())}
{
}

void mircv::XKBMapper::XkbMappingState::set_key_state(std::vector<uint32_t> const& key_state)
{
    modifier_state = mir_input_event_modifier_none;
    std::unordered_set<uint32_t> pressed_codes;
    std::string t;
    for (uint32_t scan_code : key_state)
    {
        bool already_pressed = pressed_codes.count(scan_code) > 0;

        update_state(to_xkb_scan_code(scan_code),
                     (already_pressed) ? mir_keyboard_action_up : mir_keyboard_action_down,
                     nullptr,
                     t);

        if (already_pressed)
            pressed_codes.erase(scan_code);
        else
            pressed_codes.insert(scan_code);
    }
}

bool mircv::XKBMapper::XkbMappingState::update_and_map(MirEvent& event, mircv::XKBMapper::ComposeState* compose_state)
{
    auto& key_ev = *event.to_input()->to_keyboard();
    uint32_t xkb_scan_code = to_xkb_scan_code(key_ev.scan_code());
    auto old_state = modifier_state;
    std::string key_text;
    xkb_keysym_t keysym;
    keysym = update_state(xkb_scan_code, key_ev.action(), compose_state, key_text);

    key_ev.set_keysym(keysym);
    key_ev.set_text(key_text.c_str());
    // TODO we should also indicate effective/consumed modifier state to properly
    // implement short cuts with keys that are only reachable via modifier keys
    key_ev.set_modifiers(expand_modifiers(modifier_state));

    return old_state != modifier_state;
}

xkb_keysym_t mircv::XKBMapper::XkbMappingState::update_state(uint32_t scan_code, MirKeyboardAction action, mircv::XKBMapper::ComposeState* compose_state, std::string& text)
{
    auto keysym = xkb_state_key_get_one_sym(state.get(), scan_code);
    auto const mod_change = modifier_from_xkb_scan_code(scan_code);

    // Occasionally, we see XKB_KEY_Meta_L where XKB_KEY_Alt_L is correct
    if (mod_change == mir_input_event_modifier_alt_left)
    {
        keysym = XKB_KEY_Alt_L;
    }

    if(action == mir_keyboard_action_down || action == mir_keyboard_action_repeat)
    {
        char buffer[7];
        // scan code? really? not keysym?
        xkb_state_key_get_utf8(state.get(), scan_code, buffer, sizeof(buffer));
        text = buffer;
    }

    if (compose_state)
        keysym = compose_state->update_state(keysym, action, text);

    if (action == mir_keyboard_action_up)
    {
        xkb_state_update_key(state.get(), scan_code, XKB_KEY_UP);
        // TODO get the modifier state from xkbcommon and apply it
        // for all other modifiers manually track them here:
        release_modifier(mod_change);
    }
    else if (action == mir_keyboard_action_down)
    {
        xkb_state_update_key(state.get(), scan_code, XKB_KEY_DOWN);
        // TODO get the modifier state from xkbcommon and apply it
        // for all other modifiers manually track them here:
        press_modifier(mod_change);
    }

    return keysym;
}

MirInputEventModifiers mircv::XKBMapper::XkbMappingState::modifiers() const
{
    return modifier_state;
}

mircv::XKBMapper::ComposeState* mircv::XKBMapper::get_compose_state(MirInputDeviceId id)
{
    auto dev_compose_state = device_composing.find(id);

    if (dev_compose_state != end(device_composing))
    {
        return dev_compose_state->second.get();
    }
    if (compose_table)
    {
        decltype(device_composing.begin()) insertion_pos;
        std::tie(insertion_pos, std::ignore) =
            device_composing.emplace(std::piecewise_construct,
                                     std::forward_as_tuple(id),
                                     std::forward_as_tuple(std::make_unique<ComposeState>(compose_table)));

        return insertion_pos->second.get();
    }
    return nullptr;
}

mircv::XKBMapper::ComposeState::ComposeState(XKBComposeTablePtr const& table) :
    state{make_unique_compose_state(table)}
{
}

xkb_keysym_t mircv::XKBMapper::ComposeState::update_state(xkb_keysym_t mapped_key, MirKeyboardAction action, std::string& mapped_text)
{
    // the state machine only cares about downs
    if (action == mir_keyboard_action_down)
    {
        if (xkb_compose_state_feed(state.get(), mapped_key) == XKB_COMPOSE_FEED_ACCEPTED)
        {
            auto result = xkb_compose_state_get_status(state.get());
            if (result == XKB_COMPOSE_COMPOSED)
            {
                char buffer[7];
                xkb_compose_state_get_utf8(state.get(), buffer, sizeof(buffer));
                mapped_text = buffer;
                auto composed_key_sym = xkb_compose_state_get_one_sym(state.get());
                last_composed_key = std::make_tuple(mapped_key, composed_key_sym);
                return composed_key_sym;
            }
            else if (result == XKB_COMPOSE_COMPOSING)
            {
                mapped_text = "";
                consumed_keys.insert(mapped_key);
                return XKB_KEY_NoSymbol;
            }
            else if (result == XKB_COMPOSE_CANCELLED)
            {
                consumed_keys.insert(mapped_key);
                return XKB_KEY_NoSymbol;
            }
        }
    }
    else
    {
        if (last_composed_key.is_set() &&
            mapped_key == std::get<0>(last_composed_key.value()))
        {
            if (action == mir_keyboard_action_up)
            {
                mapped_text = "";
                return std::get<1>(last_composed_key.consume());
            }
            else
            {
                // on repeat get the text of the compose state
                char buffer[7];
                xkb_compose_state_get_utf8(state.get(), buffer, sizeof(buffer));
                mapped_text = buffer;
                return std::get<1>(last_composed_key.value());
            }
        }
        if (consumed_keys.erase(mapped_key))
        {
            mapped_text = "";
            return XKB_KEY_NoSymbol;
        }
    }

    return mapped_key;
}


void mircv::XKBMapper::XkbMappingState::press_modifier(MirInputEventModifiers mod)
{
    if (is_toggle_modifier(mod))
        modifier_state ^= mod;
    else
        modifier_state |= mod;
}

void mircv::XKBMapper::XkbMappingState::release_modifier(MirInputEventModifiers mod)
{
    if (!is_toggle_modifier(mod))
        modifier_state &= ~mod;
}
