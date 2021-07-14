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
#ifndef MIR_INPUT_X_INPUT_PLATFORM_H_
#define MIR_INPUT_X_INPUT_PLATFORM_H_

#include "mir/input/platform.h"
#include <memory>
#include <functional>
#include <optional>
#include <vector>
#include <set>
#include <chrono>
#include <xcb/xcb.h>

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace mir
{

namespace dispatch
{
class ReadableFd;
}

namespace X
{
class X11Resources;
}

namespace input
{

namespace X
{
class XInputDevice;

class XInputPlatform : public input::Platform
{
public:
    explicit XInputPlatform(
        std::shared_ptr<input::InputDeviceRegistry> const& input_device_registry,
        std::shared_ptr<mir::X::X11Resources> const& x11_resources);
    ~XInputPlatform();

    std::shared_ptr<dispatch::Dispatchable> dispatchable() override;
    void start() override;
    void stop() override;
    void pause_for_config() override;
    void continue_after_config() override;

private:
    void process_input_events();
    void process_input_event(xcb_generic_event_t* event);
    void process_xkb_event(xcb_generic_event_t* event);
    void key_pressed(std::optional<std::chrono::nanoseconds> event_time, xcb_keycode_t key);
    void key_released(std::optional<std::chrono::nanoseconds> event_time, xcb_keycode_t key);
    /// Defer work until all pending events are processed. Should only be called while processing events.
    void defer(std::function<void()>&& work);
    std::shared_ptr<mir::X::X11Resources> const x11_resources;
    std::shared_ptr<dispatch::ReadableFd> const xcon_dispatchable;
    std::shared_ptr<input::InputDeviceRegistry> const registry;
    std::shared_ptr<XInputDevice> const core_keyboard;
    std::shared_ptr<XInputDevice> const core_pointer;
    xcb_query_extension_reply_t const* const xkb_extension;
    xkb_context* const xkb_ctx;
    xkb_keymap* const keymap;
    xkb_state* const key_state;
    std::set<xcb_keycode_t> pressed_keys;
    std::set<xcb_keycode_t> modifiers;
    bool kbd_grabbed;
    bool ptr_grabbed;
    std::vector<std::function<void()>> deferred;
    /// Called with the next pending event before it's processed normally. If the event that is being processed is the
    /// last that is currently available, called with nullopt (the next event is NOT waited for). If this function
    /// returns true it "consumes" the event. If it returns false, the event is sent to process_input_event()
    std::optional<std::function<bool(std::optional<xcb_generic_event_t*> event)>> next_pending_event_callback;
};

}
}
}

#endif // MIR_INPUT_X_INPUT_PLATFORM_H_
