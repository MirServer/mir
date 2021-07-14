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
 * Authored by:
 *   Andreas Pokorny <andreas.pokorny@canonical.com>
 */

#include "default_event_builder.h"
#include "mir/time/clock.h"
#include "mir/input/seat.h"
#include "mir/events/event_builders.h"
#include "mir/cookie/authority.h"

#include <algorithm>

namespace me = mir::events;
namespace mi = mir::input;

mi::DefaultEventBuilder::DefaultEventBuilder(
    MirInputDeviceId device_id,
    std::shared_ptr<time::Clock> const& clock,
    std::shared_ptr<mir::cookie::Authority> const& cookie_authority,
    std::shared_ptr<mi::Seat> const& seat)
    : device_id(device_id),
      clock(clock),
      timestamp_offset(Timestamp::max()),
      cookie_authority(cookie_authority),
      seat(seat)
{
}

void mi::DefaultEventBuilder::calibrate_timestamps(bool enable)
{
    if (enable)
    {
        // Reset calibration
        timestamp_offset = Timestamp::max();
    }
    else
    {
        // Disable calibration
        timestamp_offset = Timestamp{0};
    }
}

mir::EventUPtr mi::DefaultEventBuilder::key_event(
    Timestamp source_timestamp,
    MirKeyboardAction action,
    xkb_keysym_t keysym,
    int scan_code)
{
    auto const timestamp = calibrate_timestamp(source_timestamp);
    auto const cookie = cookie_authority->make_cookie(timestamp.count());
    return me::make_key_event(
        device_id, timestamp, cookie->serialize(), action, keysym, scan_code, mir_input_event_modifier_none);
}

mir::EventUPtr mi::DefaultEventBuilder::pointer_event(
    Timestamp source_timestamp,
    MirPointerAction action,
    MirPointerButtons buttons_pressed,
    float hscroll_value, float vscroll_value,
    float relative_x_value, float relative_y_value)
{
    const float x_axis_value = 0;
    const float y_axis_value = 0;
    std::vector<uint8_t> vec_cookie{};
    auto const timestamp = calibrate_timestamp(source_timestamp);
    if (action == mir_pointer_action_button_up || action == mir_pointer_action_button_down)
    {
        auto const cookie = cookie_authority->make_cookie(timestamp.count());
        vec_cookie = cookie->serialize();
    }
    return me::make_pointer_event(
        device_id, timestamp, vec_cookie, mir_input_event_modifier_none, action, buttons_pressed, x_axis_value,
        y_axis_value,
        hscroll_value, vscroll_value, relative_x_value, relative_y_value);
}

mir::EventUPtr mi::DefaultEventBuilder::pointer_event(
    Timestamp source_timestamp,
    MirPointerAction action,
    MirPointerButtons buttons_pressed,
    float x_axis, float y_axis,
    float hscroll_value, float vscroll_value,
    float relative_x_value, float relative_y_value)
{
    std::vector<uint8_t> vec_cookie{};
    auto const timestamp = calibrate_timestamp(source_timestamp);
    if (action == mir_pointer_action_button_up || action == mir_pointer_action_button_down)
    {
        auto const cookie = cookie_authority->make_cookie(timestamp.count());
        vec_cookie = cookie->serialize();
    }
    return me::make_pointer_event(
        device_id, timestamp, vec_cookie, mir_input_event_modifier_none, action, buttons_pressed, x_axis, y_axis,
        hscroll_value, vscroll_value, relative_x_value, relative_y_value);
}

mir::EventUPtr mi::DefaultEventBuilder::touch_event(
    Timestamp source_timestamp,
    std::vector<events::ContactState> const& contacts)
{
    std::vector<uint8_t> vec_cookie{};
    auto const timestamp = calibrate_timestamp(source_timestamp);
    for (auto const& contact : contacts)
    {
        if (contact.action == mir_touch_action_up || contact.action == mir_touch_action_down)
        {
            auto const cookie = cookie_authority->make_cookie(timestamp.count());
            vec_cookie = cookie->serialize();
            break;
        }
    }
    return me::make_touch_event(device_id, timestamp, vec_cookie, mir_input_event_modifier_none, contacts);
}

auto mi::DefaultEventBuilder::calibrate_timestamp(Timestamp timestamp) -> Timestamp
{
    auto offset = timestamp_offset.load();
    if (offset == Timestamp::max())
    {
        // If used from multiple threads this could happen multiple times at once, but that's not a problem
        offset = clock->now().time_since_epoch() - timestamp;
        timestamp_offset.store(offset);
    }
    return timestamp + offset;
}
