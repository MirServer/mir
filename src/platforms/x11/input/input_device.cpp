/*
 * Copyright © 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3,
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
 * Authored by: Cemil Azizoglu <cemil.azizoglu@canonical.com>
 */

#include "input_device.h"

#include "mir/input/pointer_settings.h"
#include "mir/input/touchpad_settings.h"
#include "mir/input/touchscreen_settings.h"
#include "mir/input/input_device_info.h"
#include "mir/input/device_capability.h"
#include "mir/input/event_builder.h"
#include "mir/input/input_sink.h"

#include <xcb/xcb.h>

namespace mi = mir::input;
namespace geom = mir::geometry;
namespace mix = mi::X;

namespace
{
MirPointerButtons to_mir_button(int button)
{
    static auto const button_side = 8;
    static auto const button_extra = 9;
    if (button == XCB_BUTTON_INDEX_1)
        return mir_pointer_button_primary;
    if (button == XCB_BUTTON_INDEX_2)  // tertiary (middle) button is button 2 in X
        return mir_pointer_button_tertiary;
    if (button == XCB_BUTTON_INDEX_3)
        return mir_pointer_button_secondary;
    if (button == button_side)
        return mir_pointer_button_side;
    if (button == button_extra)
        return mir_pointer_button_extra;
    return 0;
}

MirPointerButtons to_mir_button_state(int x_button_key_state)
{
    MirPointerButtons button_state = 0;
    if (x_button_key_state & XCB_BUTTON_MASK_1)
        button_state |= mir_pointer_button_primary;
    if (x_button_key_state & XCB_BUTTON_MASK_2)
        button_state |= mir_pointer_button_tertiary;
    if (x_button_key_state & XCB_BUTTON_MASK_3)
        button_state |= mir_pointer_button_secondary;
    if (x_button_key_state & XCB_BUTTON_MASK_4)
        button_state |= mir_pointer_button_back;
    if (x_button_key_state & XCB_BUTTON_MASK_5)
        button_state |= mir_pointer_button_forward;
    return button_state;
}
}

mix::XInputDevice::XInputDevice(InputDeviceInfo const& device_info)
    : info(device_info)
{
}

void mix::XInputDevice::start(InputSink* input_sink, EventBuilder* event_builder)
{
    sink = input_sink;
    builder = event_builder;
}

void mix::XInputDevice::stop()
{
    sink = nullptr;
    builder = nullptr;
}

mi::InputDeviceInfo mix::XInputDevice::get_device_info()
{
    // TODO Make use if X11-XInput2 to get raw device information
    // and support other devices than just the unified pointer and
    // keyboards.
    return info;
}

mir::optional_value<mi::PointerSettings> mix::XInputDevice::get_pointer_settings() const
{
    mir::optional_value<PointerSettings> ret;
    if (contains(info.capabilities, DeviceCapability::pointer))
        ret = PointerSettings();

    return ret;
}

void mix::XInputDevice::apply_settings(PointerSettings const&)
{
}

mir::optional_value<mi::TouchpadSettings> mix::XInputDevice::get_touchpad_settings() const
{
    optional_value<TouchpadSettings> ret;
    if (contains(info.capabilities, DeviceCapability::touchpad))
        ret = TouchpadSettings();

    return ret;
}

void mix::XInputDevice::apply_settings(TouchpadSettings const&)
{
}

mir::optional_value<mi::TouchscreenSettings> mix::XInputDevice::get_touchscreen_settings() const
{
    optional_value<TouchscreenSettings> ret;
    if (contains(info.capabilities, DeviceCapability::touchscreen))
        ret = TouchscreenSettings();

    return ret;
}

void mix::XInputDevice::apply_settings(TouchscreenSettings const&)
{
}

bool mix::XInputDevice::started() const
{
    return sink && builder;
}

void mix::XInputDevice::key_press(EventTime event_time, xkb_keysym_t keysym, int scan_code)
{
    sink->handle_input(builder->key_event(
        event_time,
        mir_keyboard_action_down,
        keysym,
        scan_code));

}

void mix::XInputDevice::key_release(EventTime event_time, xkb_keysym_t keysym, int scan_code)
{
    sink->handle_input(builder->key_event(
        event_time,
        mir_keyboard_action_up,
        keysym,
        scan_code));
}

void mix::XInputDevice::update_button_state(int button)
{
    button_state = to_mir_button_state(button);
}

void mix::XInputDevice::pointer_press(
    EventTime event_time,
    int button,
    mir::geometry::PointF pos,
    mir::geometry::DisplacementF scroll)
{
    button_state |= to_mir_button(button);
    auto const movement = pos - pointer_pos;
    pointer_pos = pos;
    sink->handle_input(
        builder->pointer_event(
            event_time,
            mir_pointer_action_button_down,
            button_state,
            pointer_pos.x.as_value(),
            pointer_pos.y.as_value(),
            scroll.dx.as_value(),
            scroll.dy.as_value(),
            movement.dx.as_value(),
            movement.dy.as_value()
            )
        );
}

void mix::XInputDevice::pointer_release(
    EventTime event_time,
    int button,
    mir::geometry::PointF pos,
    mir::geometry::DisplacementF scroll)
{
    button_state &= ~to_mir_button(button);
    auto const movement = pos - pointer_pos;
    pointer_pos = pos;
    sink->handle_input(
        builder->pointer_event(
            event_time,
            mir_pointer_action_button_up,
            button_state,
            pointer_pos.x.as_value(),
            pointer_pos.y.as_value(),
            scroll.dx.as_value(),
            scroll.dy.as_value(),
            movement.dx.as_value(),
            movement.dy.as_value()
            )
        );

}

void mix::XInputDevice::pointer_motion(
    EventTime event_time,
    mir::geometry::PointF pos,
    mir::geometry::DisplacementF scroll)
{
    auto const movement = pos - pointer_pos;
    pointer_pos = pos;
    sink->handle_input(
        builder->pointer_event(
            event_time,
            mir_pointer_action_motion,
            button_state,
            pointer_pos.x.as_value(),
            pointer_pos.y.as_value(),
            scroll.dx.as_value(),
            scroll.dy.as_value(),
            movement.dx.as_value(),
            movement.dy.as_value()
            )
        );
}
