/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranties of MERCHANTABILITY,
 * SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "miroil/event_builder.h"
#include "mir_toolkit/events/input/input_event.h"
#include "mir_toolkit/mir_cookie.h"
#include "mir/events/event_builders.h"

namespace miroil {
    
EventBuilder::EventBuilder()
: event_info_vector(10)    
{
}

EventBuilder::~EventBuilder() = default;
    
void EventBuilder::add_touch(MirEvent &event, MirTouchId touch_id, MirTouchAction action,
    MirTouchTooltype tooltype, float x_axis_value, float y_axis_value,
    float pressure_value, float touch_major_value, float touch_minor_value, float size_value)
{
    mir::events::add_touch(event, touch_id, action, tooltype, x_axis_value, y_axis_value,
                           pressure_value, touch_major_value, touch_minor_value, size_value);
}

// Key event
mir::EventUPtr EventBuilder::make_key_event(MirInputDeviceId device_id, std::chrono::nanoseconds timestamp,
    std::vector<uint8_t> const& cookie, MirKeyboardAction action, xkb_keysym_t key_code,
    int scan_code, MirInputEventModifiers modifiers)
{
    return mir::events::make_key_event(device_id, timestamp,
                                   cookie, action, key_code,
                                   scan_code, modifiers);
}

// Touch event
mir::EventUPtr EventBuilder::make_touch_event(MirInputDeviceId device_id, std::chrono::nanoseconds timestamp,
    std::vector<uint8_t> const& mac, MirInputEventModifiers modifiers)
{
    return mir::events::make_touch_event(device_id, timestamp,
                                   mac, modifiers);
}

// Pointer event
mir::EventUPtr EventBuilder::make_pointer_event(MirInputDeviceId device_id, std::chrono::nanoseconds timestamp,
    std::vector<uint8_t> const& mac, MirInputEventModifiers modifiers, MirPointerAction action,
    MirPointerButtons buttons_pressed,
    float x_axis_value, float y_axis_value,
    float hscroll_value, float vscroll_value,
    float relative_x_value, float relative_y_value)
{
    return mir::events::make_pointer_event(device_id, timestamp,
                                   mac, modifiers, action,
                                   buttons_pressed,
                                   x_axis_value, y_axis_value,
                                   hscroll_value, vscroll_value,
                                   relative_x_value, relative_y_value);
}

EventBuilder::EventInfo *EventBuilder::find_info(ulong qtTimestamp)
{
    for (size_t i = 0; i < event_info_count; ++i) {
        if (event_info_vector[i].timestamp == qtTimestamp) {
            return &event_info_vector[i];
        }
    }
    return nullptr;
}

void EventBuilder::store(const MirInputEvent *mirInputEvent, ulong qtTimestamp)
{
    EventInfo &eventInfo = event_info_vector[next_index];
    eventInfo.store(mirInputEvent, qtTimestamp);

    next_index = (next_index + 1) % event_info_vector.size();

    if (event_info_count < event_info_vector.size()) {
        ++event_info_count;
    }
}

void EventBuilder::EventInfo::store(const MirInputEvent *iev, ulong qtTimestamp)
{
    this->timestamp = qtTimestamp;
    device_id = mir_input_event_get_device_id(iev);
    if (mir_input_event_has_cookie(iev))
    {
        auto cookie_ptr = mir_input_event_get_cookie(iev);
        cookie.resize(mir_cookie_buffer_size(cookie_ptr));
        mir_cookie_to_buffer(cookie_ptr, cookie.data(), cookie.size());
        mir_cookie_release(cookie_ptr);
    } else {
        cookie.resize(0);
    }
    if (mir_input_event_type_pointer == mir_input_event_get_type(iev))
    {
        auto pev = mir_input_event_get_pointer_event(iev);
        relative_x = mir_pointer_event_axis_value(pev, mir_pointer_axis_relative_x);
        relative_y = mir_pointer_event_axis_value(pev, mir_pointer_axis_relative_y);
    }
}

}
