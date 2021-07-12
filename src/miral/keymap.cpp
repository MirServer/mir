/*
 * Copyright © 2016-2018 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#include "miral/keymap.h"

#include <mir/fd.h>
#include <mir/input/input_device_observer.h>
#include <mir/input/input_device_hub.h>
#include <mir/input/device.h>
#include <mir/options/option.h>
#include <mir/server.h>
#include <mir/udev/wrapper.h>

#include <mir/input/parameter_keymap.h>
#include <mir/input/mir_keyboard_config.h>

#define MIR_LOG_COMPONENT "miral::Keymap"
#include <mir/log.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace mi = mir::input;

namespace
{
std::string keymap_default()
{
    static auto const default_keymap = "us";

    namespace mu = mir::udev;

    mu::Enumerator input_enumerator{std::make_shared<mu::Context>()};
    input_enumerator.match_subsystem("input");
    input_enumerator.scan_devices();

    for (auto& device : input_enumerator)
    {
        if (auto const layout  = device.property("XKBLAYOUT"))
        {
            auto const options = device.property("XKBOPTIONS");
            auto const variant = device.property("XKBVARIANT");

            std::string keymap{layout};

            if (variant || options)
            {
                keymap += '+';

                if (variant)
                {
                    keymap += variant;
                }

                if (options)
                {
                    keymap += '+';
                    keymap += options;
                }
            }

            return keymap;
        }
    }

    return default_keymap;
}

char const* const keymap_option = "keymap";
}

struct miral::Keymap::Self : mir::input::InputDeviceObserver
{
    Self(std::string const& keymap) : layout{}, variant{}
    {
        set_keymap(keymap);
    }

    void set_keymap(std::string const& keymap)
    {
        std::lock_guard<decltype(mutex)> lock{mutex};
        auto get_next_token = [km = keymap]() mutable
        {
            auto const i = km.find('+');
            auto ret = km.substr(0,i);
            if (i != std::string::npos)
                km = km.substr(i+1, std::string::npos);
            else
                km = "";
            return ret;
        };

        layout = get_next_token();
        variant = get_next_token();
        options = get_next_token();

        for (auto const& keyboard : keyboards)
            apply_keymap(keyboard);
    }

    void device_added(std::shared_ptr<mir::input::Device> const& device) override
    try
    {
        std::lock_guard<decltype(mutex)> lock{mutex};

        if (mir::contains(device->capabilities(), mir::input::DeviceCapability::keyboard))
            add_keyboard(device);
    }
    catch (...)
    {
        mir::log(mir::logging::Severity::warning, MIR_LOG_COMPONENT, std::current_exception(),
                 "problem adding device (" + device->name() + ")");
    }

    void device_changed(std::shared_ptr<mir::input::Device> const& device) override
    {
        std::lock_guard<decltype(mutex)> lock{mutex};

        auto const keyboard = std::find(begin(keyboards), end(keyboards), device);

        if (mir::contains(device->capabilities(), mir::input::DeviceCapability::keyboard))
        {
            if (keyboard == end(keyboards))
                add_keyboard(device);
        }
        else
        {
            if (keyboard != end(keyboards))
                keyboards.erase(keyboard);
        }
    }

    void add_keyboard(std::shared_ptr<mir::input::Device> const& keyboard)
    {
        keyboards.push_back(keyboard);
        apply_keymap(keyboard);
    }

    void apply_keymap(std::shared_ptr<mir::input::Device> const& keyboard)
    {
        std::string model = mi::ParameterKeymap::default_model;
        auto const keyboard_config = keyboard->keyboard_configuration();
        if (keyboard_config.is_set())
        {
            model = keyboard_config.value().device_keymap()->model();
        }
        std::shared_ptr<mi::Keymap> keymap{std::make_shared<mi::ParameterKeymap>(model, layout, variant, options)};
        keyboard->apply_keyboard_configuration(std::move(keymap));
    }

    void device_removed(std::shared_ptr<mir::input::Device> const& device) override
    try
    {
        std::lock_guard<decltype(mutex)> lock{mutex};

        if (mir::contains(device->capabilities(), mir::input::DeviceCapability::keyboard))
            keyboards.erase(std::find(begin(keyboards), end(keyboards), device));
    }
    catch (...)
    {
        mir::log(mir::logging::Severity::warning, MIR_LOG_COMPONENT, std::current_exception(),
            "problem removing device (" + device->name() + ")");
    }

    void changes_complete() override
    {
    }

    std::mutex mutable mutex;
    std::string layout;
    std::string variant;
    std::string options;
    std::vector<std::shared_ptr<mir::input::Device>> keyboards;
};

miral::Keymap::Keymap() :
    self{std::make_shared<Self>(std::string{})}
{
}

miral::Keymap::Keymap(std::string const& keymap) :
    self{std::make_shared<Self>(keymap)}
{
}

miral::Keymap::~Keymap() = default;

miral::Keymap::Keymap(Keymap const&) = default;

auto miral::Keymap::operator=(Keymap const& rhs) -> Keymap& = default;

void miral::Keymap::operator()(mir::Server& server) const
{
    if (self->layout.empty())
        server.add_configuration_option(keymap_option, "keymap <layout>[+<variant>[+<options>]], e,g, \"gb\" or \"cz+qwerty\" or \"de++compose:caps\"", keymap_default());

    server.add_init_callback([this, &server]
        {
            if (self->layout.empty())
                self->set_keymap(server.get_options()->get<std::string>(keymap_option));

            server.the_input_device_hub()->add_observer(self);
        });
}

void miral::Keymap::set_keymap(std::string const& keymap)
{
    self->set_keymap(keymap);
}
