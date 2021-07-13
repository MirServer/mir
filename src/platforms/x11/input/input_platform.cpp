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

#include "input_platform.h"
#include "input_device.h"

#include "mir/c_memory.h"
#include "mir/graphics/display_configuration.h"
#include "mir/input/input_device_registry.h"
#include "mir/input/input_device_info.h"
#include "mir/dispatch/readable_fd.h"
#include "../x11_resources.h"

#define MIR_LOG_COMPONENT "x11-input"
#include "mir/log.h"

#include <linux/input.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <chrono>

#include <xcb/xfixes.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>

// xcb/xkb.h has a struct member named "explicit", which C++ does not like
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define explicit explicit_
#include <xcb/xkb.h>
#undef explicit
#ifdef __clang__
#pragma clang diagnostic pop
#endif

// Due to a bug in Unity when keyboard is grabbed,
// client cannot be resized. This helps in debugging.
#define GRAB_KBD

namespace mi = mir::input;
namespace geom = mir::geometry;
namespace md = mir::dispatch;
namespace mix = mi::X;
namespace mx = mir::X;

namespace
{
auto const button_scroll_up = XCB_BUTTON_INDEX_4;
auto const button_scroll_down = XCB_BUTTON_INDEX_5;
auto const button_scroll_left = 6;
auto const button_scroll_right = 7;
auto const scroll_factor = 10;

auto init_xkb_extension(mir::X::X11Resources* x11_resources) -> xcb_query_extension_reply_t const*
{
    auto const xkb_extension = x11_resources->conn->get_extension_data(&xcb_xkb_id);
    if (!xkb_extension || !xkb_extension->present)
    {
        mir::log_warning("XKB X11 extension not available, keyboard will not work");
        return nullptr;
    }

    auto const required_major = 1;
    auto const required_minor = 0;
    auto const cookie = xcb_xkb_use_extension(x11_resources->conn->connection(), required_major, required_minor);
    auto const reply = mir::make_unique_cptr(
        xcb_xkb_use_extension_reply(x11_resources->conn->connection(), cookie, nullptr));
    if (!reply || !reply->supported)
    {
        mir::log_warning("XKB X11 extension does not have a supported version, keyboard will not work");
        return nullptr;
    }

    auto const map_mask =
        XCB_XKB_MAP_PART_KEY_TYPES |
        XCB_XKB_MAP_PART_KEY_SYMS |
        XCB_XKB_MAP_PART_MODIFIER_MAP |
        XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
        XCB_XKB_MAP_PART_KEY_ACTIONS |
        XCB_XKB_MAP_PART_KEY_BEHAVIORS |
        XCB_XKB_MAP_PART_VIRTUAL_MODS |
        XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;

    auto const event_mask =
        XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY |
        XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
        XCB_XKB_EVENT_TYPE_STATE_NOTIFY;

    xcb_xkb_select_events(
        x11_resources->conn->connection(),
        XCB_XKB_ID_USE_CORE_KBD,
        event_mask,
        0,
        event_mask,
        map_mask,
        map_mask,
        0);

    return xkb_extension;
}

auto get_pos_on_output(mir::X::X11Resources* x11_resources, xcb_window_t x11_window, float x, float y) -> geom::PointF
{
    geom::PointF pos{x, y};
    x11_resources->with_output_for_window(
        x11_window,
        [&](std::optional<mx::X11Resources::VirtualOutput const*> output)
        {
            if (output)
            {
                float const inv_scale{1 / output.value()->configuration().scale};
                geom::PointF const top_left{output.value()->configuration().top_left};
                pos = top_left + as_displacement(pos) * inv_scale;
            }
            else
            {
                mir::log_warning(
                    "X11 window %d does not map to any known output, not applying input transformation",
                    x11_window);
            }
        });
    return pos;
}

void window_resized(mir::X::X11Resources* x11_resources, xcb_window_t x11_window, geom::Size const& size)
{
    x11_resources->with_output_for_window(
        x11_window,
        [&](std::optional<mx::X11Resources::VirtualOutput*> output)
        {
            if (output)
            {
                output.value()->set_size(size);
            }
        });
}

auto xcb_keycode_get_scan_code(xcb_keycode_t xcb_keycode) -> int
{
    return xcb_keycode - 8;
}
}

mix::XInputPlatform::XInputPlatform(std::shared_ptr<mi::InputDeviceRegistry> const& input_device_registry,
                                    std::shared_ptr<mir::X::X11Resources> const& x11_resources) :
    x11_resources{x11_resources},
    xcon_dispatchable(std::make_shared<md::ReadableFd>(
        mir::Fd{mir::IntOwnedFd{x11_resources->conn->get_file_descriptor()}}, [this]()
        {
            process_input_events();
        })),
    registry(input_device_registry),
    core_keyboard(std::make_shared<mix::XInputDevice>(
            mi::InputDeviceInfo{"x11-keyboard-device", "x11-key-dev-1", mi::DeviceCapability::keyboard})),
    core_pointer(std::make_shared<mix::XInputDevice>(
            mi::InputDeviceInfo{"x11-mouse-device", "x11-mouse-dev-1", mi::DeviceCapability::pointer})),
    xkb_extension{init_xkb_extension(x11_resources.get())},
    xkb_ctx{xkb_context_new(XKB_CONTEXT_NO_FLAGS)},
    keymap{xkb_x11_keymap_new_from_device(
        xkb_ctx,
        x11_resources->conn->connection(),
        xkb_x11_get_core_keyboard_device_id(x11_resources->conn->connection()),
        XKB_KEYMAP_COMPILE_NO_FLAGS)},
    key_state{xkb_x11_state_new_from_device(
        keymap,
        x11_resources->conn->connection(),
        xkb_x11_get_core_keyboard_device_id(x11_resources->conn->connection()))},
    kbd_grabbed{false},
    ptr_grabbed{false}
{
    if (!xkb_ctx || !keymap || !key_state)
    {
        log_error("Failed to set up X11 keymap");
    }

    x11_resources->conn->flush();
}

mix::XInputPlatform::~XInputPlatform()
{
    xkb_state_unref(key_state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(xkb_ctx);
}

void mix::XInputPlatform::start()
{
    registry->add_device(core_keyboard);
    registry->add_device(core_pointer);
}

std::shared_ptr<md::Dispatchable> mix::XInputPlatform::dispatchable()
{
    return xcon_dispatchable;
}

void mix::XInputPlatform::stop()
{
    registry->remove_device(core_keyboard);
    registry->remove_device(core_pointer);
}

void mix::XInputPlatform::pause_for_config()
{
}

void mix::XInputPlatform::continue_after_config()
{
}

void mix::XInputPlatform::process_input_events()
{
    if (x11_resources->conn->has_error())
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("XCB connection error"));
    }

    while(auto const event = make_unique_cptr(x11_resources->conn->poll_for_event()))
    {
        if (core_keyboard->started() && core_pointer->started())
        {
            bool consumed = false;
            if (next_pending_event_callback)
            {
                (*next_pending_event_callback)(event.get());
                next_pending_event_callback = {};
            }
            if (!consumed)
            {
                process_input_event(event.get());
            }
        }
        else
        {
            mir::log_error("input event received with no sink to handle it");
        }
    }

    if (next_pending_event_callback)
    {
        (*next_pending_event_callback)(std::nullopt);
        // Ignore result because "consuming" a null event is meaningless
        next_pending_event_callback.reset();
    }

    while (!deferred.empty())
    {
        auto const local_deferred = std::move(deferred);
        deferred.clear();
        for (auto const& callback : local_deferred)
        {
            callback();
        }
    }

    x11_resources->conn->flush();
}

void mix::XInputPlatform::process_input_event(xcb_generic_event_t* event)
{
    switch (event->response_type & ~0x80)
    {
    case XCB_FOCUS_IN:
    {
#ifdef GRAB_KBD
        auto const focus_in_ev = reinterpret_cast<xcb_focus_in_event_t*>(event);
        if (!kbd_grabbed && (
            focus_in_ev->mode == XCB_NOTIFY_MODE_NORMAL ||
            focus_in_ev->mode == XCB_NOTIFY_MODE_WHILE_GRABBED))
        {
            auto const cookie = xcb_grab_keyboard(
                x11_resources->conn->connection(),
                1,
                focus_in_ev->event,
                XCB_CURRENT_TIME,
                XCB_GRAB_MODE_ASYNC,
                XCB_GRAB_MODE_ASYNC);
            kbd_grabbed = true;
            defer([this, cookie]()
                {
                    auto const reply = make_unique_cptr(xcb_grab_keyboard_reply(
                        x11_resources->conn->connection(),
                        cookie,
                        nullptr));
                    if (!reply || reply->status != XCB_GRAB_STATUS_SUCCESS)
                    {
                        kbd_grabbed = false;
                    }
                });
        }
#endif
    }   break;

    case XCB_FOCUS_OUT:
    {
        // key_released() will modify pressed_keys, so make a local copy
        std::vector<xcb_keycode_t> const pressed{pressed_keys.begin(), pressed_keys.end()};
        for (auto const& key : pressed)
        {
            // Only release keys that aren't modifiers
            if (modifiers.find(key) == modifiers.end())
            {
                // FOCUS_OUT doesn't have a timestamp
                key_released(std::nullopt, key);
            }
        }

#ifdef GRAB_KBD
        auto const focus_out_ev = reinterpret_cast<xcb_focus_out_event_t*>(event);
        if (kbd_grabbed && (
            focus_out_ev->mode == XCB_NOTIFY_MODE_NORMAL ||
            focus_out_ev->mode == XCB_NOTIFY_MODE_WHILE_GRABBED))
        {
            xcb_ungrab_keyboard(x11_resources->conn->connection(), XCB_CURRENT_TIME);
            kbd_grabbed = false;
        }
#endif
    }   break;
    case XCB_ENTER_NOTIFY:
    {
        auto const enter_ev = reinterpret_cast<xcb_enter_notify_event_t*>(event);

        if (!ptr_grabbed && kbd_grabbed)
        {
            auto const cookie = xcb_grab_pointer(
                x11_resources->conn->connection(),
                1,
                enter_ev->event,
                0,
                XCB_GRAB_MODE_ASYNC,
                XCB_GRAB_MODE_ASYNC,
                XCB_NONE,
                XCB_NONE,
                XCB_CURRENT_TIME);
            xcb_xfixes_hide_cursor(x11_resources->conn->connection(), enter_ev->event);
            ptr_grabbed = true;
            defer([this, cookie]()
                {
                    auto const reply = make_unique_cptr(xcb_grab_pointer_reply(
                        x11_resources->conn->connection(),
                        cookie,
                        nullptr));
                    if (!reply || reply->status != XCB_GRAB_STATUS_SUCCESS)
                    {
                        ptr_grabbed = false;
                    }
                });
        }
    }   break;

    case XCB_LEAVE_NOTIFY:
    {
        auto const leave_ev = reinterpret_cast<xcb_leave_notify_event_t*>(event);

        if (ptr_grabbed)
        {
            xcb_ungrab_pointer(x11_resources->conn->connection(), XCB_CURRENT_TIME);
            xcb_xfixes_show_cursor(x11_resources->conn->connection(), leave_ev->event);
            ptr_grabbed = false;
        }
    }   break;

    case XCB_KEY_PRESS:
    {
        auto const press_ev = reinterpret_cast<xcb_key_press_event_t*>(event);
        auto const event_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::milliseconds{press_ev->time});
        key_pressed(event_time, press_ev->detail);
    }   break;

    case XCB_KEY_RELEASE:
    {
        auto const release_ev = reinterpret_cast<xcb_key_release_event_t*>(event);

        // Key repeats look like a release and an immediate press with the same timestamp. The only way to detect and
        // discard them is by peaking at the next event.

        next_pending_event_callback = [this, xcb_keycode = release_ev->detail, time = release_ev->time](auto next_event)
            {
                if (next_event && (next_event.value()->response_type & ~0x80) == XCB_KEY_PRESS)
                {
                    auto const press_ev = reinterpret_cast<xcb_key_press_event_t*>(next_event.value());
                    if (press_ev->detail == xcb_keycode && press_ev->time == time)
                    {
                        // This event is a key repeat, so ignore it. Also consume the next event by returning true.
                        return true;
                    }
                }

                auto const event_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::milliseconds{time});
                key_released(event_time, xcb_keycode);
                return false; // do not consume next event, process it normally
            };
    }   break;

    case XCB_BUTTON_PRESS:
    {
        auto const press_ev = reinterpret_cast<xcb_button_press_event_t*>(event);

        auto const event_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::milliseconds{press_ev->time});
        auto const pos = get_pos_on_output(x11_resources.get(), press_ev->event, press_ev->event_x, press_ev->event_y);
        core_pointer->update_button_state(press_ev->state);

        switch (press_ev->detail)
        {
        case button_scroll_up:
            core_pointer->pointer_motion(event_time, pos, {0, -scroll_factor});
            break;

        case button_scroll_down:
            core_pointer->pointer_motion(event_time, pos, {0, scroll_factor});
            break;

        case button_scroll_left:
            core_pointer->pointer_motion(event_time, pos, {-scroll_factor, 0});
            break;

        case button_scroll_right:
            core_pointer->pointer_motion(event_time, pos, {scroll_factor, 0});
            break;

        default:
            core_pointer->pointer_press(event_time, press_ev->detail, pos, {});
        }
    }   break;

    case XCB_BUTTON_RELEASE:
    {
        auto const release_ev = reinterpret_cast<xcb_button_release_event_t*>(event);

        core_pointer->update_button_state(release_ev->state);

        switch (release_ev->detail)
        {
        case button_scroll_up:
        case button_scroll_down:
        case button_scroll_left:
        case button_scroll_right:
            break;

        default:
            auto const event_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::milliseconds{release_ev->time});
            auto const pos = get_pos_on_output(
                x11_resources.get(),
                release_ev->event,
                release_ev->event_x,
                release_ev->event_y);
            core_pointer->pointer_release(event_time, release_ev->detail, pos, {});
        }
    }   break;

    case XCB_MOTION_NOTIFY:
    {
        auto const motion_ev = reinterpret_cast<xcb_motion_notify_event_t*>(event);
        core_pointer->update_button_state(motion_ev->state);
        auto const event_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::milliseconds{motion_ev->time});
        auto pos = get_pos_on_output(x11_resources.get(), motion_ev->event, motion_ev->event_x, motion_ev->event_y);
        core_pointer->pointer_motion(event_time, pos, {});
    }   break;

    case XCB_CONFIGURE_NOTIFY:
    {
        auto const config_ev = reinterpret_cast<xcb_configure_notify_event_t*>(event);
        geom::Size const new_size{config_ev->width, config_ev->height};
        // Caching the size would require mapping windows to sizes. We already do this later on. No need to here.
        window_resized(x11_resources.get(), config_ev->event, new_size);
    }   break;

    // TODO: watch for changes in the keymap?

    case XCB_CLIENT_MESSAGE:
        // Assume this is a WM_DELETE_WINDOW message
        log_info("Exiting");
        kill(getpid(), SIGTERM);
        break;

    default:
        break;
    }

    if (xkb_extension && event->response_type == xkb_extension->first_event)
    {
        process_xkb_event(event);
    }
}

void mix::XInputPlatform::process_xkb_event(xcb_generic_event_t* event)
{
    struct XkbEventGeneric
    {
        uint8_t response_type;
        uint8_t xkbType;
    };

    switch (reinterpret_cast<XkbEventGeneric*>(event)->xkbType)
    {
    case XCB_XKB_NEW_KEYBOARD_NOTIFY:
    {
        auto const new_keyboard_ev = reinterpret_cast<xcb_xkb_new_keyboard_notify_event_t*>(event);
        if (new_keyboard_ev->changed & XCB_XKB_NKN_DETAIL_KEYCODES)
        {
            // TODO: update keymap?
        }
    }   break;

    case XCB_XKB_MAP_NOTIFY:
    {
        auto const map_ev = reinterpret_cast<xcb_xkb_map_notify_event_t*>(event);
        (void)map_ev;
        // TODO: update keymap?
    }   break;

    case XCB_XKB_STATE_NOTIFY:
    {
        auto const state_ev = reinterpret_cast<xcb_xkb_state_notify_event_t*>(event);
        xkb_state_update_mask(
            key_state,
            state_ev->baseMods,
            state_ev->latchedMods,
            state_ev->lockedMods,
            state_ev->baseGroup,
            state_ev->latchedGroup,
            state_ev->lockedGroup);
        // This only works for modifiers, but unlike the normal events it tracks presses and releases when the window
        // is not focused
        if (state_ev->keycode != 0)
        {
            modifiers.insert(state_ev->keycode);
        }
        auto const event_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::milliseconds{state_ev->time});
        switch (state_ev->eventType)
        {
        case XCB_KEY_PRESS:
            key_pressed(event_time, state_ev->keycode);
            break;

        case XCB_KEY_RELEASE:
            key_released(event_time, state_ev->keycode);
            break;

        default:;
        }
    }   break;

    default:;
    }
}

void mix::XInputPlatform::key_pressed(std::optional<std::chrono::nanoseconds> event_time, xcb_keycode_t xcb_keycode)
{
    if (pressed_keys.insert(xcb_keycode).second)
    {
        xkb_keysym_t const keysym = xkb_state_key_get_one_sym(key_state, xcb_keycode);
        auto const scan_code = xcb_keycode_get_scan_code(xcb_keycode);
        core_keyboard->key_press(event_time, keysym, scan_code);
    }
}

void mix::XInputPlatform::key_released(std::optional<std::chrono::nanoseconds> event_time, xcb_keycode_t xcb_keycode)
{
    if (pressed_keys.erase(xcb_keycode))
    {
        xkb_keysym_t keysym = xkb_state_key_get_one_sym(key_state, xcb_keycode);
        auto const scan_code = xcb_keycode_get_scan_code(xcb_keycode);
        core_keyboard->key_release(event_time, keysym, scan_code);
    }
}

void mix::XInputPlatform::defer(std::function<void()>&& work)
{
    // We've presumably just fired off a request we're going to wait on later. Flush so the roudtrip starts now rather
    // than when we get around to waiting on the reply.
    x11_resources->conn->flush();
    deferred.push_back(std::move(work));
}
