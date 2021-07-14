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

#ifndef MIR_INPUT_DEFAULT_INPUT_DEVICE_HUB_H_
#define MIR_INPUT_DEFAULT_INPUT_DEVICE_HUB_H_

#include "default_event_builder.h"

#include "mir/input/input_device_registry.h"
#include "mir/input/input_sink.h"
#include "mir/input/seat.h"
#include "mir/input/input_device_hub.h"
#include "mir/input/input_device_observer.h"
#include "mir/input/input_device_info.h"
#include "mir/input/mir_input_config.h"
#include "mir/thread_safe_list.h"

#include "mir_toolkit/event.h"

#include <linux/input.h>
#include <vector>
#include <memory>
#include <mutex>
#include <optional>

namespace mir
{
class ServerActionQueue;
class ServerStatusListener;
namespace cookie
{
class Authority;
}
namespace time
{
class Clock;
}
namespace dispatch
{
class Dispatchable;
class MultiplexingDispatchable;
class ActionQueue;
}
namespace input
{
class InputSink;
class InputDeviceObserver;
class DefaultDevice;
class Seat;
class KeyMapper;
class DefaultInputDeviceHub;

struct ExternalInputDeviceHub : InputDeviceHub
{
    ExternalInputDeviceHub(std::shared_ptr<InputDeviceHub> const& actual_hub,
                           std::shared_ptr<ServerActionQueue> const& observer_queue);
    ~ExternalInputDeviceHub();

    void add_observer(std::shared_ptr<InputDeviceObserver> const&) override;
    void remove_observer(std::weak_ptr<InputDeviceObserver> const&) override;
    void for_each_input_device(std::function<void(Device const& device)> const& callback) override;
    void for_each_mutable_input_device(std::function<void(Device& device)> const& callback) override;

private:
    struct Internal;
    std::shared_ptr<Internal> const data;
    std::shared_ptr<InputDeviceHub> const hub;
};

class DefaultInputDeviceHub :
    public InputDeviceRegistry,
    public InputDeviceHub
{
public:
    DefaultInputDeviceHub(
        std::shared_ptr<Seat> const& seat,
        std::shared_ptr<dispatch::MultiplexingDispatchable> const& input_multiplexer,
        std::shared_ptr<time::Clock> const& clock,
        std::shared_ptr<cookie::Authority> const& cookie_authority,
        std::shared_ptr<KeyMapper> const& key_mapper,
        std::shared_ptr<ServerStatusListener> const& server_status_listener);

    // InputDeviceRegistry - calls from mi::Platform
    void add_device(std::shared_ptr<InputDevice> const& device) override;
    void remove_device(std::shared_ptr<InputDevice> const& device) override;

    // InputDeviceHub - calls from server components / shell
    void add_observer(std::shared_ptr<InputDeviceObserver> const&) override;
    void remove_observer(std::weak_ptr<InputDeviceObserver> const&) override;
    void for_each_input_device(std::function<void(Device const& device)> const& callback) override;
    void for_each_mutable_input_device(std::function<void(Device& device)> const& callback) override;
private:
    void add_device_handle(std::lock_guard<std::recursive_mutex> const&, std::shared_ptr<DefaultDevice> const& handle);
    void remove_device_handle(std::lock_guard<std::recursive_mutex> const&, MirInputDeviceId id);
    void device_changed(Device* dev);
    void complete_transaction();
    MirInputDeviceId create_new_device_id(std::lock_guard<std::recursive_mutex> const&);
    void store_device_config(std::lock_guard<std::recursive_mutex> const&, DefaultDevice const& dev);
    auto restore_or_create_device(
        std::lock_guard<std::recursive_mutex> const& lock,
        InputDevice& dev,
        std::shared_ptr<dispatch::ActionQueue> const& queue) -> std::shared_ptr<DefaultDevice>;
    auto get_stored_device_config(
        std::lock_guard<std::recursive_mutex> const&,
        std::string const& id) -> std::optional<MirInputDevice>;

    std::shared_ptr<Seat> const seat;
    std::shared_ptr<dispatch::MultiplexingDispatchable> const input_dispatchable;
    std::shared_ptr<dispatch::ActionQueue> const device_queue;
    std::shared_ptr<time::Clock> const clock;
    std::shared_ptr<cookie::Authority> const cookie_authority;
    std::shared_ptr<KeyMapper> const key_mapper;
    std::shared_ptr<ServerStatusListener> const server_status_listener;
    ThreadSafeList<std::shared_ptr<InputDeviceObserver>> observers;

    /// Does not guarantee it's own threadsafety, non-const methods should not be called from multiple threads at once
    struct RegisteredDevice : public InputSink
    {
    public:
        RegisteredDevice(
            std::shared_ptr<InputDevice> const& dev,
            MirInputDeviceId dev_id,
            std::shared_ptr<dispatch::ActionQueue> const& multiplexer,
            std::shared_ptr<time::Clock> const& clock,
            std::shared_ptr<cookie::Authority> const& cookie_authority,
            std::shared_ptr<DefaultDevice> const& handle);
        void handle_input(std::shared_ptr<MirEvent> const& event) override;
        geometry::Rectangle bounding_rectangle() const override;
        input::OutputInfo output_info(uint32_t output_id) const override;
        bool device_matches(std::shared_ptr<InputDevice> const& dev) const;
        void start(std::shared_ptr<Seat> const& seat, std::shared_ptr<dispatch::MultiplexingDispatchable> const& dispatchable);
        void stop(std::shared_ptr<dispatch::MultiplexingDispatchable> const& dispatchable);
        MirInputDeviceId id();
        std::shared_ptr<Seat> seat;
        const std::shared_ptr<DefaultDevice> handle;

        void key_state(std::vector<uint32_t> const& scan_codes) override;
        void pointer_state(MirPointerButtons buttons) override;
    private:
        MirInputDeviceId device_id;
        std::unique_ptr<DefaultEventBuilder> builder;
        std::shared_ptr<time::Clock> const clock;
        std::shared_ptr<cookie::Authority> cookie_authority;
        std::shared_ptr<InputDevice> const device;
        std::shared_ptr<dispatch::ActionQueue> queue;
    };

    // Needs to be a recursive mutex so that initial device notifications can be sent under lock in add_observer()
    std::recursive_mutex mutex;
    std::vector<std::shared_ptr<Device>> handles;
    std::vector<std::unique_ptr<RegisteredDevice>> devices;
    /// Nullopt when no transaction is in progress. Set to an empty vector when a transaction starts, and change
    /// notications are sent about each device contained in the vector when the transaction is done.
    std::optional<std::vector<std::shared_ptr<Device>>> pending_changes;
    std::vector<MirInputDevice> stored_devices;
    MirInputDeviceId device_id_generator;
    bool ready{false};
};

}
}

#endif
