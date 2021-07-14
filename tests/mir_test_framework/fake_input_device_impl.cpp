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
 * Authored by: Andreas Pokorny <andreas.pokorny@canonical.com>
 */

#include "fake_input_device_impl.h"
#include "mir_test_framework/stub_input_platform.h"

#include "mir/input/input_device.h"
#include "mir/input/input_device_info.h"
#include "mir/input/input_sink.h"
#include "mir/input/pointer_settings.h"
#include "mir/input/touchpad_settings.h"
#include "mir/input/event_builder.h"
#include "mir/dispatch/action_queue.h"
#include "mir/geometry/displacement.h"
#include "src/platforms/evdev/button_utils.h"

#include "boost/throw_exception.hpp"

#include "mir/events/event_builders.h"

#include <chrono>
#include <thread>

namespace mi = mir::input;
namespace mie = mi::evdev;
namespace md = mir::dispatch;
namespace mtf = mir_test_framework;
namespace synthesis = mir::input::synthesis;

mtf::FakeInputDeviceImpl::FakeInputDeviceImpl(mi::InputDeviceInfo const& info)
    : queue{std::make_shared<md::ActionQueue>()}, device{std::make_shared<InputDevice>(info, queue)}
{
    mtf::StubInputPlatform::add(device);
}

void mtf::FakeInputDeviceImpl::emit_device_removal()
{
    mtf::StubInputPlatform::remove(device);
}

void mtf::FakeInputDeviceImpl::emit_runtime_error()
{
    queue->enqueue([]()
                   {
                       throw std::runtime_error("runtime error in input device");
                   });
}

void mtf::FakeInputDeviceImpl::emit_event(synthesis::KeyParameters const& key)
{
    queue->enqueue([this, key]()
                   {
                       device->synthesize_events(key);
                   });
}

void mtf::FakeInputDeviceImpl::emit_event(synthesis::ButtonParameters const& button)
{
    queue->enqueue([this, button]()
                   {
                       device->synthesize_events(button);
                   });
}

void mtf::FakeInputDeviceImpl::emit_event(synthesis::MotionParameters const& motion)
{
    queue->enqueue([this, motion]()
                   {
                       device->synthesize_events(motion);
                   });
}

void mtf::FakeInputDeviceImpl::emit_event(synthesis::TouchParameters const& touch)
{
    queue->enqueue([this, touch]()
                   {
                       device->synthesize_events(touch);
                   });
}

void mtf::FakeInputDeviceImpl::emit_touch_sequence(std::function<mir::input::synthesis::TouchParameters(int)> const& event_generator,
                                                   int count,
                                                   std::chrono::duration<double> delay)
{
    queue->enqueue(
        [this, event_generator, count, delay]()
        {
            auto start = std::chrono::steady_clock::now();
            for (int i = 0;i < count;++i)
            {
                std::this_thread::sleep_until(start + i * delay);
                device->synthesize_events(event_generator(i++));
                std::this_thread::yield();
            }
        });
}

void mtf::FakeInputDeviceImpl::emit_key_state(std::vector<uint32_t> const& key_syms)
{
    queue->enqueue(
        [this, key_syms]()
        {
            device->emit_key_state(key_syms);
        });
}

void mtf::FakeInputDeviceImpl::on_new_configuration_do(std::function<void(mir::input::InputDevice const& device)> callback)
{
    device->set_apply_settings_callback(callback);
}
void mtf::FakeInputDeviceImpl::InputDevice::set_apply_settings_callback(std::function<void(mir::input::InputDevice const&)> const& callback)
{
    std::lock_guard<std::mutex> lock(config_callback_mutex);
    this->callback = callback;
}

mtf::FakeInputDeviceImpl::InputDevice::InputDevice(mi::InputDeviceInfo const& info,
                                                   std::shared_ptr<mir::dispatch::Dispatchable> const& dispatchable)
    : info(info), queue{dispatchable}, buttons{0}, callback([](mir::input::InputDevice const&){})
{
    // the default setup results in a direct mapping of input velocity to output velocity.
    settings.acceleration = mir_pointer_acceleration_none;
    settings.cursor_acceleration_bias = 0.0;

    // add default setup for touchscreen..
    if (contains(info.capabilities, mi::DeviceCapability::touchscreen))
        touchscreen = mi::TouchscreenSettings{};
}

void mtf::FakeInputDeviceImpl::InputDevice::synthesize_events(synthesis::KeyParameters const& key_params)
{
    xkb_keysym_t keysym = 0;

    auto event_time = key_params.event_time.value_or(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()));

    auto input_action =
        (key_params.action == synthesis::EventAction::Down) ? mir_keyboard_action_down : mir_keyboard_action_up;

    auto key_event = builder->key_event(event_time, input_action, keysym, key_params.scancode);

    if (!sink)
        BOOST_THROW_EXCEPTION(std::runtime_error("Device is not started."));
    sink->handle_input(std::move(key_event));
}

void mtf::FakeInputDeviceImpl::InputDevice::synthesize_events(synthesis::ButtonParameters const& button)
{
    auto event_time = button.event_time.value_or(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()));
    auto action = update_buttons(button.action, mie::to_pointer_button(button.button, settings.handedness));
    auto button_event = builder->pointer_event(event_time,
                                               action,
                                               buttons,
                                               scroll.x.as_int(),
                                               scroll.y.as_int(),
                                               0.0f,
                                               0.0f);

    if (!sink)
        BOOST_THROW_EXCEPTION(std::runtime_error("Device is not started."));
    sink->handle_input(std::move(button_event));
}

MirPointerAction mtf::FakeInputDeviceImpl::InputDevice::update_buttons(synthesis::EventAction action, MirPointerButton button)
{
    if (action == synthesis::EventAction::Down)
    {
        buttons |= button;
        return mir_pointer_action_button_down;
    }
    else
    {
        buttons &= ~button;
        return mir_pointer_action_button_up;
    }
}

void mtf::FakeInputDeviceImpl::InputDevice::synthesize_events(synthesis::MotionParameters const& pointer)
{
    if (!sink)
        BOOST_THROW_EXCEPTION(std::runtime_error("Device is not started."));

    auto event_time = pointer.event_time.value_or(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()));
    // constant scaling is used here to simplify checking for the
    // expected results. Default settings of the device lead to no
    // scaling at all.
    auto const acceleration = settings.cursor_acceleration_bias + 1.0;
    auto const rel_x = pointer.rel_x * acceleration;
    auto const rel_y = pointer.rel_y * acceleration;

    auto pointer_event = builder->pointer_event(event_time,
                                                mir_pointer_action_motion,
                                                buttons,
                                                scroll.x.as_int(),
                                                scroll.y.as_int(),
                                                rel_x,
                                                rel_y);

    sink->handle_input(std::move(pointer_event));
}

void mtf::FakeInputDeviceImpl::InputDevice::synthesize_events(synthesis::TouchParameters const& touch)
{
    if (!sink)
        BOOST_THROW_EXCEPTION(std::runtime_error("Device is not started."));

    auto const event_time = touch.event_time.value_or(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()));

    auto touch_action = mir_touch_action_up;
    if (touch.action == synthesis::TouchParameters::Action::Tap)
        touch_action = mir_touch_action_down;
    else if (touch.action == synthesis::TouchParameters::Action::Move)
        touch_action = mir_touch_action_change;

    float abs_x = touch.abs_x;
    float abs_y = touch.abs_y;
    map_touch_coordinates(abs_x, abs_y);

    if (is_output_active())
    {
        auto touch_event = builder->touch_event(
            event_time,
            {{MirTouchId{1}, touch_action, mir_touch_tooltype_finger, abs_x, abs_y, 1.0f, 8.0f, 5.0f, 0.0f}});

        sink->handle_input(std::move(touch_event));
    }
}

void mtf::FakeInputDeviceImpl::InputDevice::emit_key_state(std::vector<uint32_t> const& scan_codes)
{
    sink->key_state(scan_codes);
}

mir::optional_value<mi::PointerSettings> mtf::FakeInputDeviceImpl::InputDevice::get_pointer_settings() const
{
    mir::optional_value<mi::PointerSettings> ret;
    if (!contains(info.capabilities, mi::DeviceCapability::pointer))
        return ret;

    ret = settings;
    return ret;
}

void mtf::FakeInputDeviceImpl::InputDevice::apply_settings(mi::PointerSettings const& settings)
{
    if (!contains(info.capabilities, mi::DeviceCapability::pointer))
        return;
    this->settings = settings;
    trigger_callback();
}

mir::optional_value<mi::TouchpadSettings> mtf::FakeInputDeviceImpl::InputDevice::get_touchpad_settings() const
{
    mir::optional_value<mi::TouchpadSettings> ret;
    if (contains(info.capabilities, mi::DeviceCapability::touchpad))
        ret = mi::TouchpadSettings();

    return ret;
}

void mtf::FakeInputDeviceImpl::InputDevice::apply_settings(mi::TouchpadSettings const&)
{
    // Not applicable for configuration since FakeInputDevice just
    // forwards already interpreted events.
    trigger_callback();
}

void mtf::FakeInputDeviceImpl::InputDevice::trigger_callback() const
{
    decltype(callback) stored_callback;
    {
        std::lock_guard<std::mutex> lock(config_callback_mutex);
        stored_callback = callback;
    }
    stored_callback(*this);
}

mir::optional_value<mi::TouchscreenSettings> mtf::FakeInputDeviceImpl::InputDevice::get_touchscreen_settings() const
{
    mir::optional_value<mi::TouchscreenSettings> ret;
    if (!contains(info.capabilities, mi::DeviceCapability::touchscreen))
        return ret;

    ret = touchscreen;
    return ret;
}

void mtf::FakeInputDeviceImpl::InputDevice::apply_settings(mi::TouchscreenSettings const& new_settings)
{
    if (!contains(info.capabilities, mi::DeviceCapability::touchscreen))
        return;
    this->touchscreen = new_settings;

    trigger_callback();
}

void mtf::FakeInputDeviceImpl::InputDevice::map_touch_coordinates(float& x, float& y)
{
    auto info = get_output_info();
    info.transform_to_scene(x, y);
}

void mtf::FakeInputDeviceImpl::InputDevice::start(mi::InputSink* destination, mi::EventBuilder* event_builder)
{
    // We keep track of events based on their timestamps, so we can't have those being modified
    event_builder->calibrate_timestamps(false);

    sink = destination;
    builder = event_builder;
    mtf::StubInputPlatform::register_dispatchable(queue);
}

void mtf::FakeInputDeviceImpl::InputDevice::stop()
{
    sink = nullptr;
    builder = nullptr;
    mtf::StubInputPlatform::unregister_dispatchable(queue);
}

mi::OutputInfo mtf::FakeInputDeviceImpl::InputDevice::get_output_info() const
{
    if (touchscreen.mapping_mode == mir_touchscreen_mapping_mode_to_output)
    {
        return sink->output_info(touchscreen.output_id);
    }
    else
    {
        auto scene_bbox = sink->bounding_rectangle();
        return mi::OutputInfo(
            true,
            scene_bbox.size,
            mi::OutputInfo::Matrix{{1.0f, 0.0f, float(scene_bbox.top_left.x.as_int()),
                                    0.0f, 1.0f, float(scene_bbox.top_left.y.as_int())}});
    }
}

bool mtf::FakeInputDeviceImpl::InputDevice::is_output_active() const
{
    if (!sink)
        return false;

    if (touchscreen.mapping_mode == mir_touchscreen_mapping_mode_to_output)
    {
        auto output = sink->output_info(touchscreen.output_id);
        return output.active;
    }
    return true;
}
