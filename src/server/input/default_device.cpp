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

#include "default_device.h"
#include "default_input_device_hub.h"
#include "mir/dispatch/action_queue.h"
#include "mir/input/device_capability.h"
#include "mir/input/input_device.h"
#include "mir/input/mir_touchpad_config.h"
#include "mir/input/mir_touchscreen_config.h"
#include "mir/input/mir_pointer_config.h"
#include "mir/input/key_mapper.h"

#include <boost/throw_exception.hpp>
#include <stdexcept>

namespace mi = mir::input;

mi::DefaultDevice::DefaultDevice(MirInputDeviceId id,
                                 std::shared_ptr<dispatch::ActionQueue> const& actions,
                                 InputDevice& device,
                                 std::shared_ptr<KeyMapper> const& key_mapper,
                                 std::function<void(Device*)> const& callback)
    : device_id{id},
      device{device},
      info(device.get_device_info()),
      pointer{device.get_pointer_settings()},
      touchpad{device.get_touchpad_settings()},
      touchscreen{device.get_touchscreen_settings()},
      actions{actions},
      key_mapper{key_mapper},
      device_changed_callback{callback}
{
    if (contains(info.capabilities, mi::DeviceCapability::keyboard))
    {
        keyboard = MirKeyboardConfig{};
        key_mapper->set_keymap_for_device(device_id, keyboard.value().device_keymap());
    }
}

mi::DefaultDevice::DefaultDevice(MirInputDeviceId id, std::shared_ptr<dispatch::ActionQueue> const& actions,
                                 InputDevice& device, std::shared_ptr<KeyMapper> const& key_mapper)
    : DefaultDevice(id, actions, device, key_mapper, [](Device*){})
{
}

mi::DefaultDevice::DefaultDevice(MirInputDevice const& config,
                                 std::shared_ptr<dispatch::ActionQueue> const& actions,
                                 InputDevice& device,
                                 std::shared_ptr<KeyMapper> const& key_mapper,
                                 std::function<void(Device*)> const& callback)
    : DefaultDevice(config.id(), actions, device, key_mapper, callback)
{
#define ATTEMPT(x) try{ x; }catch(...){}
    ATTEMPT(if (config.has_touchpad_config())
                set_touchpad_configuration(config.touchpad_config()));
    ATTEMPT(if (config.has_keyboard_config())
                set_keyboard_configuration(config.keyboard_config()));
    ATTEMPT(if (config.has_pointer_config())
                set_pointer_configuration(config.pointer_config()));
    ATTEMPT(if (config.has_touchscreen_config())
                set_touchscreen_configuration(config.touchscreen_config()));
#undef ATTEMPT
}

mi::DeviceCapabilities mi::DefaultDevice::capabilities() const
{
    return info.capabilities;
}

std::string mi::DefaultDevice::name() const
{
    return info.name;
}

std::string mi::DefaultDevice::unique_id() const
{
    return info.unique_id;
}

MirInputDeviceId mi::DefaultDevice::id() const
{
    return device_id;
}

mir::optional_value<MirPointerConfig> mi::DefaultDevice::pointer_configuration() const
{
    std::lock_guard<std::mutex> lock(config_mutex);
    if (!pointer.is_set())
        return {};

    auto const& settings = pointer.value();

    return MirPointerConfig(settings.handedness, settings.acceleration,
                            settings.cursor_acceleration_bias, settings.horizontal_scroll_scale,
                            settings.vertical_scroll_scale);
}

mir::optional_value<MirTouchpadConfig> mi::DefaultDevice::touchpad_configuration() const
{
    std::lock_guard<std::mutex> lock(config_mutex);
    if (!touchpad.is_set())
        return {};

    auto const& settings = touchpad.value();

    return MirTouchpadConfig(settings.click_mode, settings.scroll_mode, settings.button_down_scroll_button,
                             settings.tap_to_click, settings.disable_while_typing, settings.disable_with_mouse,
                             settings.middle_mouse_button_emulation);
}

void mi::DefaultDevice::apply_pointer_configuration(MirPointerConfig const& conf)
{
    {
        std::lock_guard<std::mutex> lock(config_mutex);
        if (!pointer.is_set())
            BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot apply a pointer configuration"));
    }

    if (conf.cursor_acceleration_bias() < -1.0 || conf.cursor_acceleration_bias() > 1.0)
        BOOST_THROW_EXCEPTION(std::invalid_argument("Cursor acceleration bias out of range"));

    set_pointer_configuration(conf);
    device_changed_callback(this);
}

void mi::DefaultDevice::set_pointer_configuration(MirPointerConfig const& conf)
{
    PointerSettings settings;
    settings.handedness = conf.handedness();
    settings.acceleration = conf.acceleration();
    settings.cursor_acceleration_bias = conf.cursor_acceleration_bias();
    settings.vertical_scroll_scale = conf.vertical_scroll_scale();
    settings.horizontal_scroll_scale = conf.horizontal_scroll_scale();

    {
        std::lock_guard<std::mutex> lock(config_mutex);
        pointer = settings;
        if (!actions) // device is disabled
            return;
        actions->enqueue([settings = std::move(settings), dev=&device]
                         {
                             dev->apply_settings(settings);
                         });
    }
}

void mi::DefaultDevice::apply_touchpad_configuration(MirTouchpadConfig const& conf)
{
    {
        std::lock_guard<std::mutex> lock(config_mutex);
        if (!touchpad.is_set())
            BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot apply a touchpad configuration"));
    }

    if (conf.scroll_mode() & mir_touchpad_scroll_mode_button_down_scroll &&
        conf.button_down_scroll_button() == mi::no_scroll_button)
        BOOST_THROW_EXCEPTION(std::invalid_argument("No scroll button configured"));

    set_touchpad_configuration(conf);
    device_changed_callback(this);
}

void mi::DefaultDevice::set_touchpad_configuration(MirTouchpadConfig const& conf)
{
    TouchpadSettings settings;
    settings.click_mode= conf.click_mode();
    settings.scroll_mode = conf.scroll_mode();
    settings.button_down_scroll_button = conf.button_down_scroll_button();
    settings.disable_with_mouse = conf.disable_with_mouse();
    settings.disable_while_typing = conf.disable_while_typing();
    settings.tap_to_click = conf.tap_to_click();
    settings.middle_mouse_button_emulation = conf.middle_mouse_button_emulation();

    {
        std::lock_guard<std::mutex> lock(config_mutex);
        touchpad = settings;
        if (!actions) // device is disabled
            return;
        actions->enqueue([settings = std::move(settings), dev=&device]
                         {
                             dev->apply_settings(settings);
                         });
    }
}

mir::optional_value<MirKeyboardConfig> mi::DefaultDevice::keyboard_configuration() const
{
    std::lock_guard<std::mutex> lock(config_mutex);
    return keyboard;
}

void mi::DefaultDevice::apply_keyboard_configuration(MirKeyboardConfig const& conf)
{
    if (!contains(info.capabilities, mi::DeviceCapability::keyboard))
        BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot apply a keyboard configuration"));

    set_keyboard_configuration(conf);
    device_changed_callback(this);
}

void mi::DefaultDevice::set_keyboard_configuration(MirKeyboardConfig const& conf)
{
    std::lock_guard<std::mutex> lock(config_mutex);
    if (!actions) // device is disabled
        return;
    if (!keyboard.value().device_keymap()->matches(*conf.device_keymap()))
        keyboard = conf;
    else
        return;
    key_mapper->set_keymap_for_device(device_id, conf.device_keymap());
}

mir::optional_value<MirTouchscreenConfig> mi::DefaultDevice::touchscreen_configuration() const
{
    if (!touchscreen.is_set())
        return {};

    auto const& settings = touchscreen.value();

    return MirTouchscreenConfig(settings.output_id, settings.mapping_mode);
}

void mi::DefaultDevice::apply_touchscreen_configuration(MirTouchscreenConfig const& config)
{
    if (!touchscreen.is_set())
        BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot apply a touchscreen configuration"));

    set_touchscreen_configuration(config);
    device_changed_callback(this);
}

void mi::DefaultDevice::set_touchscreen_configuration(MirTouchscreenConfig const& config)
{
    TouchscreenSettings settings;
    settings.output_id = config.output_id();
    settings.mapping_mode = config.mapping_mode();

    {
        std::lock_guard<std::mutex> lock(config_mutex);
        touchscreen = settings;

        if (!actions) // device is disabled
            return;
        actions->enqueue([settings = std::move(settings), dev=&device]
                         {
                             dev->apply_settings(settings);
                         });
    }
}

MirInputDevice mi::DefaultDevice::config() const
{
    auto stored_dev = MirInputDevice(
        id(),
        capabilities(),
        name(),
        unique_id());
    auto pointer_conf = pointer_configuration();
    if (pointer_conf.is_set())
        stored_dev.set_pointer_config(pointer_conf.value());
    auto touchpad_conf = touchpad_configuration();
    if (touchpad_conf.is_set())
        stored_dev.set_touchpad_config(touchpad_conf.value());
    auto keyboard_conf = keyboard_configuration();
    if (keyboard_conf.is_set())
        stored_dev.set_keyboard_config(keyboard_conf.value());
    auto touchscreen_conf = touchscreen_configuration();
    if (touchscreen_conf.is_set())
        stored_dev.set_touchscreen_config(touchscreen_conf.value());

    return stored_dev;
}

void mi::DefaultDevice::disable_queue()
{
    std::lock_guard<std::mutex> lock(config_mutex);
    actions.reset();
}
