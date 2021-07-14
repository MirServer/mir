/*
 * Copyright © 2015 Canonical Ltd.
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
 * Authored by: Cemil Azizoglu <cemil.azizoglu@canonical.com>
 */

#ifndef MIR_INPUT_X_INPUT_DEVICE_H_
#define MIR_INPUT_X_INPUT_DEVICE_H_

#include "mir/input/input_device.h"
#include "mir/input/input_device_info.h"
#include "mir_toolkit/event.h"
#include "mir/geometry/point_f.h"
#include "mir/geometry/displacement_f.h"
#include "mir/optional_value.h"

#include <chrono>
#include <optional>

namespace mir
{
namespace input
{

namespace X
{

class XInputDevice : public input::InputDevice
{
public:
    using EventTime = std::optional<std::chrono::nanoseconds>;

    XInputDevice(InputDeviceInfo const& info);

    std::shared_ptr<dispatch::Dispatchable> dispatchable();
    void start(InputSink* destination, EventBuilder* builder) override;
    void stop() override;
    InputDeviceInfo get_device_info() override;

    optional_value<PointerSettings> get_pointer_settings() const override;
    void apply_settings(PointerSettings const& settings) override;
    optional_value<TouchpadSettings> get_touchpad_settings() const override;
    void apply_settings(TouchpadSettings const& settings) override;
    virtual optional_value<TouchscreenSettings> get_touchscreen_settings() const override;
    virtual void apply_settings(TouchscreenSettings const&) override;

    bool started() const;
    void key_press(EventTime event_time, xkb_keysym_t keysym, int scan_code);
    void key_release(EventTime event_time, xkb_keysym_t keysym, int scan_code);
    void update_button_state(int button);
    void pointer_press(
        EventTime event_time,
        int button,
        mir::geometry::PointF pos,
        mir::geometry::DisplacementF scroll);
    void pointer_release(
        EventTime event_time,
        int button,
        mir::geometry::PointF pos,
        mir::geometry::DisplacementF scroll);
    void pointer_motion(
        EventTime event_time,
        mir::geometry::PointF pos,
        mir::geometry::DisplacementF scroll);

private:
    MirPointerButtons button_state{0};
    InputSink* sink{nullptr};
    EventBuilder* builder{nullptr};
    geometry::PointF pointer_pos;
    InputDeviceInfo info;
};

}

}
}

#endif // MIR_INPUT_X_INPUT_DEVICE_H_
