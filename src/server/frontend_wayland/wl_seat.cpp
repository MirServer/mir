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
 * Authored by: Christopher James Halse Rogers <christopher.halse.rogers@canonical.com>
 */

#include "wayland_wrapper.h"

#include "wl_seat.h"

#include "wayland_utils.h"
#include "wl_surface.h"
#include "wl_keyboard.h"
#include "wl_pointer.h"
#include "wl_touch.h"

#include "mir/input/input_device_observer.h"
#include "mir/input/input_device_hub.h"
#include "mir/input/seat.h"
#include "mir/input/device.h"
#include "mir/input/parameter_keymap.h"
#include "mir/input/mir_keyboard_config.h"

#include <mutex>
#include <unordered_set>
#include <algorithm>

namespace mf = mir::frontend;
namespace mi = mir::input;
namespace mw = mir::wayland;

template<class T>
class mf::WlSeat::ListenerList
{
public:
    ListenerList() = default;

    ListenerList(ListenerList&&) = delete;
    ListenerList(ListenerList const&) = delete;
    ListenerList& operator=(ListenerList const&) = delete;

    void register_listener(wl_client* client, T* listener)
    {
        listeners[client].push_back(listener);
    }

    void unregister_listener(wl_client* client, T const* listener)
    {
        std::vector<T*>& client_listeners = listeners[client];
        client_listeners.erase(
            std::remove(
                client_listeners.begin(),
                client_listeners.end(),
                listener),
            client_listeners.end());
        if (client_listeners.size() == 0)
            listeners.erase(client);
    }

    void for_each(wl_client* client, std::function<void(T*)> func)
    {
        for (auto listener: listeners[client])
            func(listener);
    }

private:
    std::unordered_map<wl_client*, std::vector<T*>> listeners;
};

class mf::WlSeat::ConfigObserver : public mi::InputDeviceObserver
{
public:
    ConfigObserver(
        std::shared_ptr<mi::Keymap> const& keymap,
        std::function<void(std::shared_ptr<mi::Keymap> const&)> const& on_keymap_commit)
        : current_keymap{keymap},
          pending_keymap{nullptr},
          on_keymap_commit{on_keymap_commit}
    {
    }

    void device_added(std::shared_ptr<input::Device> const& device) override;
    void device_changed(std::shared_ptr<input::Device> const& device) override;
    void device_removed(std::shared_ptr<input::Device> const& device) override;
    void changes_complete() override;

private:
    std::shared_ptr<mi::Keymap> current_keymap;
    std::shared_ptr<mi::Keymap> pending_keymap;
    std::function<void(std::shared_ptr<mi::Keymap> const&)> const on_keymap_commit;
};

void mf::WlSeat::ConfigObserver::device_added(std::shared_ptr<input::Device> const& device)
{
    if (auto keyboard_config = device->keyboard_configuration())
    {
        pending_keymap = keyboard_config.value().device_keymap();
    }
}

void mf::WlSeat::ConfigObserver::device_changed(std::shared_ptr<input::Device> const& device)
{
    if (auto keyboard_config = device->keyboard_configuration())
    {
        pending_keymap = keyboard_config.value().device_keymap();
    }
}

void mf::WlSeat::ConfigObserver::device_removed(std::shared_ptr<input::Device> const& /*device*/)
{
}

void mf::WlSeat::ConfigObserver::changes_complete()
{
    if (pending_keymap && !pending_keymap->matches(*current_keymap))
    {
        current_keymap = std::move(pending_keymap);
        on_keymap_commit(current_keymap);
    }

    pending_keymap.reset();
}

class mf::WlSeat::Instance : public wayland::Seat
{
public:
    Instance(wl_resource* new_resource, mf::WlSeat* seat, bool enable_key_repeat);

    mf::WlSeat* const seat;

private:
    bool const enable_key_repeat;
    void get_pointer(wl_resource* new_pointer) override;
    void get_keyboard(wl_resource* new_keyboard) override;
    void get_touch(wl_resource* new_touch) override;
};

mf::WlSeat::WlSeat(
    wl_display* display,
    std::shared_ptr<mi::InputDeviceHub> const& input_hub,
    std::shared_ptr<mi::Seat> const& seat,
    bool enable_key_repeat)
    :   Global(display, Version<6>()),
        keymap{std::make_shared<input::ParameterKeymap>()},
        config_observer{
            std::make_shared<ConfigObserver>(
                keymap,
                [this](std::shared_ptr<mi::Keymap> const& new_keymap)
                {
                    keymap = new_keymap;
                })},
        pointer_listeners{std::make_shared<ListenerList<WlPointer>>()},
        keyboard_listeners{std::make_shared<ListenerList<WlKeyboard>>()},
        touch_listeners{std::make_shared<ListenerList<WlTouch>>()},
        input_hub{input_hub},
        seat{seat},
        enable_key_repeat{enable_key_repeat}
{
    input_hub->add_observer(config_observer);
    add_focus_listener(&focus);
}

mf::WlSeat::~WlSeat()
{
    input_hub->remove_observer(config_observer);
}

auto mf::WlSeat::from(struct wl_resource* seat) -> WlSeat*
{
    return static_cast<mf::WlSeat::Instance*>(wayland::Seat::from(seat))->seat;
}

void mf::WlSeat::for_each_listener(wl_client* client, std::function<void(WlPointer*)> func)
{
    pointer_listeners->for_each(client, func);
}

void mf::WlSeat::for_each_listener(wl_client* client, std::function<void(WlKeyboard*)> func)
{
    keyboard_listeners->for_each(client, func);
}

void mf::WlSeat::for_each_listener(wl_client* client, std::function<void(WlTouch*)> func)
{
    touch_listeners->for_each(client, func);
}

void mf::WlSeat::notify_focus(wl_client *focus)
{
    if (focus != focused_client)
    {
        focused_client = focus;
        for (auto const listener : focus_listeners)
            listener->focus_on(focus);
    }
}

void mf::WlSeat::bind(wl_resource* new_wl_seat)
{
    new Instance{new_wl_seat, this, enable_key_repeat};
}

mf::WlSeat::Instance::Instance(wl_resource* new_resource, mf::WlSeat* seat, bool enable_key_repeat)
    : mw::Seat(new_resource, Version<6>()),
      seat{seat},
      enable_key_repeat{enable_key_repeat}
{
    // TODO: Read the actual capabilities. Do we have a keyboard? Mouse? Touch?
    send_capabilities_event(Capability::pointer | Capability::keyboard | Capability::touch);
    if (version_supports_name())
        send_name_event("seat0");
}

void mf::WlSeat::Instance::get_pointer(wl_resource* new_pointer)
{
    auto const pointer = new WlPointer{new_pointer};
    seat->pointer_listeners->register_listener(client, pointer);
    pointer->add_destroy_listener(
        [listeners = seat->pointer_listeners, listener = pointer, client = client]()
        {
            listeners->unregister_listener(client, listener);
        });
}

void mf::WlSeat::Instance::get_keyboard(wl_resource* new_keyboard)
{
    auto const keyboard = new WlKeyboard{
        new_keyboard,
        seat->keymap,
        [seat = seat->seat]()
        {
            std::unordered_set<uint32_t> pressed_keys;

            auto const ev = seat->create_device_state();
            auto const state_event = mir_event_get_input_device_state_event(ev.get());
            for (
                auto dev = 0u;
                dev < mir_input_device_state_event_device_count(state_event);
                ++dev)
            {
                for (
                    auto idx = 0u;
                    idx < mir_input_device_state_event_device_pressed_keys_count(state_event, dev);
                    ++idx)
                {
                    pressed_keys.insert(
                        mir_input_device_state_event_device_pressed_keys_for_index(
                            state_event,
                            dev,
                            idx));
                }
            }

            return std::vector<uint32_t>{pressed_keys.begin(), pressed_keys.end()};
        },
        enable_key_repeat
    };

    seat->keyboard_listeners->register_listener(client, keyboard);
    keyboard->add_destroy_listener(
        [listeners = seat->keyboard_listeners, listener = keyboard, client = client]()
        {
            listeners->unregister_listener(client, listener);
        });
}

void mf::WlSeat::Instance::get_touch(wl_resource* new_touch)
{
    auto const touch = new WlTouch{new_touch};
    seat->touch_listeners->register_listener(client, touch);
    touch->add_destroy_listener(
        [listeners = seat->touch_listeners, listener = touch, client = client]()
        {
            listeners->unregister_listener(client, listener);
        });
}

auto mf::WlSeat::current_focused_client() const -> wl_client*
{
    return focused_client;
}

void mf::WlSeat::add_focus_listener(ListenerTracker* listener)
{
    focus_listeners.push_back(listener);
}

void mf::WlSeat::remove_focus_listener(ListenerTracker* listener)
{
    focus_listeners.erase(remove(begin(focus_listeners), end(focus_listeners), listener), end(focus_listeners));
}

void mf::WlSeat::server_restart()
{
    if (focus.client)
        for_each_listener(focus.client, [](WlKeyboard* keyboard) { keyboard->resync_keyboard(); });
}
