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
 * Authored by: William Wold <william.wold@canonical.com>
 */

#include "virtual_keyboard_v1.h"
#include "virtual-keyboard-unstable-v1_wrapper.h"
#include "wayland_wrapper.h"
#include "wl_seat.h"

#include "mir/input/event_builder.h"
#include "mir/input/input_sink.h"
#include "mir/input/input_device_info.h"
#include "mir/input/pointer_settings.h"
#include "mir/input/touchpad_settings.h"
#include "mir/input/touchscreen_settings.h"
#include "mir/input/input_device_registry.h"
#include "mir/input/input_device.h"
#include "mir/fatal.h"
#include "mir/log.h"

#include <mutex>

namespace mf = mir::frontend;
namespace mw = mir::wayland;
namespace mi = mir::input;

namespace
{
auto unique_keyboard_name() -> std::string
{
    static std::mutex mutex;
    static int id = 0;
    std::lock_guard<std::mutex> lock{mutex};
    auto const result = "virt-key-" + std::to_string(id);
    id++;
    return result;
}

auto mir_keyboard_action(uint32_t wayland_state) -> MirKeyboardAction
{
    switch (wayland_state)
    {
    case mw::Keyboard::KeyState::pressed:
        return mir_keyboard_action_down;
    case mw::Keyboard::KeyState::released:
        return mir_keyboard_action_up;
    default:
        // Protocol does not provide an appropriate error code, so throw a generic runtime_error. This will be expressed
        // to the client as an implementation error
        BOOST_THROW_EXCEPTION(std::runtime_error(
            "Invalid virtual keyboard key state " + std::to_string(wayland_state)));
    }
}
}

namespace mir
{
namespace frontend
{
struct VirtualKeyboardV1Ctx
{
    std::shared_ptr<mi::InputDeviceRegistry> const device_registry;
};

class VirtualKeyboardManagerV1Global
    : public wayland::VirtualKeyboardManagerV1::Global
{
public:
    VirtualKeyboardManagerV1Global(wl_display* display, std::shared_ptr<VirtualKeyboardV1Ctx> const& ctx);

private:
    void bind(wl_resource* new_resource) override;

    std::shared_ptr<VirtualKeyboardV1Ctx> const ctx;
};

class VirtualKeyboardManagerV1
    : public wayland::VirtualKeyboardManagerV1
{
public:
    VirtualKeyboardManagerV1(wl_resource* resource, std::shared_ptr<VirtualKeyboardV1Ctx> const& ctx);

private:
    void create_virtual_keyboard(struct wl_resource* seat, struct wl_resource* id) override;

    std::shared_ptr<VirtualKeyboardV1Ctx> const ctx;
};

class VirtualKeyboardDevice
    : public input::InputDevice
{
public:
    VirtualKeyboardDevice()
        : info{"virtual-keyboard", unique_keyboard_name(), mi::DeviceCapability::keyboard}
    {
    }

    void use(std::function<void(input::InputSink*, input::EventBuilder*)> const& fn)
    {
        std::lock_guard<std::mutex> lock{mutex};
        if (sink && builder)
        {
            fn(sink, builder);
        }
    }

private:
    void start(input::InputSink* new_sink, input::EventBuilder* new_builder) override
    {
        std::lock_guard<std::mutex> lock{mutex};
        sink = new_sink;
        builder = new_builder;
    }

    void stop() override
    {
        std::lock_guard<std::mutex> lock{mutex};
        sink = nullptr;
        builder = nullptr;
    }

    auto get_device_info() -> input::InputDeviceInfo override
    {
        return info;
    }

    auto get_pointer_settings() const -> optional_value<input::PointerSettings> override { return {}; }
    void apply_settings(input::PointerSettings const&) override {}
    auto get_touchpad_settings() const -> optional_value<input::TouchpadSettings> override { return {}; }
    void apply_settings(input::TouchpadSettings const&) override {}
    auto get_touchscreen_settings() const -> optional_value<input::TouchscreenSettings> override { return {}; }
    void apply_settings(input::TouchscreenSettings const&) override {}

    std::mutex mutex;
    input::InputSink* sink{nullptr};
    input::EventBuilder* builder{nullptr};
    input::InputDeviceInfo info;
};

class VirtualKeyboardV1
    : public wayland::VirtualKeyboardV1
{
public:
    VirtualKeyboardV1(wl_resource* resource, WlSeat& seat, std::shared_ptr<VirtualKeyboardV1Ctx> const& ctx);
    ~VirtualKeyboardV1();

private:
    void keymap(uint32_t format, mir::Fd fd, uint32_t size) override;
    void key(uint32_t time, uint32_t key, uint32_t state) override;
    void modifiers(uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) override;

    std::shared_ptr<VirtualKeyboardV1Ctx> const ctx;
    std::shared_ptr<VirtualKeyboardDevice> const device;
};
}
}

auto mf::create_virtual_keyboard_manager_v1(
    wl_display* display,
    std::shared_ptr<mi::InputDeviceRegistry> const& device_registry) -> std::shared_ptr<VirtualKeyboardManagerV1Global>
{
    auto ctx = std::shared_ptr<VirtualKeyboardV1Ctx>{new VirtualKeyboardV1Ctx{
        device_registry,
    }};
    return std::make_shared<VirtualKeyboardManagerV1Global>(display, std::move(ctx));
}

mf::VirtualKeyboardManagerV1Global::VirtualKeyboardManagerV1Global(
    wl_display* display,
    std::shared_ptr<VirtualKeyboardV1Ctx> const& ctx)
    : Global{display, Version<1>()},
      ctx{ctx}
{
}

void mf::VirtualKeyboardManagerV1Global::bind(wl_resource* new_resource)
{
    new VirtualKeyboardManagerV1{new_resource, ctx};
}

mf::VirtualKeyboardManagerV1::VirtualKeyboardManagerV1(
    wl_resource* resource,
    std::shared_ptr<VirtualKeyboardV1Ctx> const& ctx)
    : wayland::VirtualKeyboardManagerV1{resource, Version<1>()},
      ctx{ctx}
{
}

void mf::VirtualKeyboardManagerV1::create_virtual_keyboard(struct wl_resource* seat, struct wl_resource* id)
{
    auto const wl_seat = WlSeat::from(seat);
    if (!wl_seat)
    {
        fatal_error("create_virtual_keyboard() received invalid wl_seat");
    }
    new VirtualKeyboardV1{id, *wl_seat, ctx};
}

mf::VirtualKeyboardV1::VirtualKeyboardV1(
    wl_resource* resource,
    WlSeat& seat,
    std::shared_ptr<VirtualKeyboardV1Ctx> const& ctx)
    : wayland::VirtualKeyboardV1{resource, Version<1>()},
      ctx{ctx},
      device{std::make_shared<VirtualKeyboardDevice>()}
{
    (void)seat;
    ctx->device_registry->add_device(device);
}

mf::VirtualKeyboardV1::~VirtualKeyboardV1()
{
    ctx->device_registry->remove_device(device);
}

void mf::VirtualKeyboardV1::keymap(uint32_t format, mir::Fd fd, uint32_t size)
{
    (void)format;
    (void)fd;
    (void)size;
    // TODO
    log_info("Ignoring zwp_virtual_keyboard_v1.keymap()");
}

void mf::VirtualKeyboardV1::key(uint32_t time, uint32_t key, uint32_t state)
{
    (void)time;
    device->use([&](input::InputSink* sink, input::EventBuilder* builder)
        {
            sink->handle_input(builder->key_event(std::chrono::nanoseconds{}, mir_keyboard_action(state), 0, key));
        });
}

void mf::VirtualKeyboardV1::modifiers(
    uint32_t mods_depressed,
    uint32_t mods_latched,
    uint32_t mods_locked,
    uint32_t group)
{
    (void)mods_depressed;
    (void)mods_latched;
    (void)mods_locked;
    (void)group;
    // TODO
    log_info("Ignoring zwp_virtual_keyboard_v1.modifiers()");
}
