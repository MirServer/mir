/*
 * Copyright © 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Brandon Schaefer <brandon.schaefer@canonical.com>
 */

#include "mir/events/input_event.h"
#include "mir/events/keyboard_event.h"

MirKeyboardEvent::MirKeyboardEvent() :
    MirInputEvent(mir_input_event_type_key)
{
}

auto MirKeyboardEvent::clone() const -> MirKeyboardEvent*
{
    return new MirKeyboardEvent{*this};
}

MirKeyboardAction MirKeyboardEvent::action() const
{
    return action_;
}

void MirKeyboardEvent::set_action(MirKeyboardAction action)
{
    action_ = action;
}

int32_t MirKeyboardEvent::keysym() const
{
    return keysym_;
}

void MirKeyboardEvent::set_keysym(int32_t keysym)
{
    keysym_ = keysym;
}

int32_t MirKeyboardEvent::scan_code() const
{
    return scan_code_;
}

void MirKeyboardEvent::set_scan_code(int32_t scan_code)
{
    scan_code_ = scan_code;
}

char const* MirKeyboardEvent::text() const
{
    return text_.c_str();
}

void MirKeyboardEvent::set_text(char const* str)
{
    text_ = str;
}

std::shared_ptr<mir::input::Keymap> MirKeyboardEvent::keymap() const
{
    return keymap_;
}

void MirKeyboardEvent::set_keymap(std::shared_ptr<mir::input::Keymap> keymap)
{
    keymap_ = std::move(keymap);
}
