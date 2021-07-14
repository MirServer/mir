/*
 * Copyright © 2013-2020 Canonical Ltd.
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
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#include "mir/default_server_configuration.h"

#include "key_repeat_dispatcher.h"
#include "event_filter_chain_dispatcher.h"
#include "config_changer.h"
#include "cursor_controller.h"
#include "touchspot_controller.h"
#include "null_input_manager.h"
#include "null_input_dispatcher.h"
#include "null_input_targeter.h"
#include "builtin_cursor_images.h"
#include "default_input_device_hub.h"
#include "default_input_manager.h"
#include "surface_input_dispatcher.h"
#include "basic_seat.h"
#include "seat_observer_multiplexer.h"

#include "mir/input/touch_visualizer.h"
#include "mir/input/input_probe.h"
#include "mir/input/platform.h"
#include "mir/input/xkb_mapper.h"
#include "mir/input/vt_filter.h"
#include "mir/options/configuration.h"
#include "mir/options/option.h"
#include "mir/dispatch/multiplexing_dispatchable.h"
#include "mir/compositor/scene.h"
#include "mir/emergency_cleanup.h"
#include "mir/main_loop.h"
#include "mir/abnormal_exit.h"
#include "mir/glib_main_loop.h"
#include "mir/log.h"
#include "mir/shared_library.h"
#include "mir/dispatch/action_queue.h"
#include "mir/console_services.h"
#include "mir/log.h"

#include "mir_toolkit/cursors.h"

namespace mi = mir::input;
namespace mr = mir::report;
namespace ms = mir::scene;
namespace mg = mir::graphics;
namespace mgn = mg::nested;
namespace msh = mir::shell;
namespace md = mir::dispatch;

std::shared_ptr<mi::CompositeEventFilter>
mir::DefaultServerConfiguration::the_composite_event_filter()
{
    return composite_event_filter(
        [this]()
        {
            return the_event_filter_chain_dispatcher();
        });
}

std::shared_ptr<mi::EventFilterChainDispatcher>
mir::DefaultServerConfiguration::the_event_filter_chain_dispatcher()
{
    return event_filter_chain_dispatcher(
        [this]() -> std::shared_ptr<mi::EventFilterChainDispatcher>
        {
            auto const make_default_filter_list =
                [this]() -> std::vector<std::weak_ptr<mi::EventFilter>>
                {
                    try
                    {
                        default_filter = std::make_shared<mi::VTFilter>(
                            the_console_services()->create_vt_switcher());
                    }
                    catch (std::exception const& err)
                    {
                        mir::log(
                            mir::logging::Severity::informational,
                            "VT switch key handler",
                            "No VT switching support available: %s",
                            err.what());
                        return {};
                    }
                    return {default_filter};
                };
            return std::make_shared<mi::EventFilterChainDispatcher>(
                make_default_filter_list(),
                the_surface_input_dispatcher());
        });
}

std::shared_ptr<msh::InputTargeter>
mir::DefaultServerConfiguration::the_input_targeter()
{
    return input_targeter(
        [this]() -> std::shared_ptr<msh::InputTargeter>
        {
            auto const options = the_options();
            if (!options->get<bool>(options::enable_input_opt))
                return std::make_shared<mi::NullInputTargeter>();
            else
                return the_surface_input_dispatcher();
        });
}

std::shared_ptr<mi::SurfaceInputDispatcher>
mir::DefaultServerConfiguration::the_surface_input_dispatcher()
{
    return surface_input_dispatcher(
        [this]()
        {
            return std::make_shared<mi::SurfaceInputDispatcher>(the_input_scene());
        });
}

std::shared_ptr<mi::InputDispatcher>
mir::DefaultServerConfiguration::the_input_dispatcher()
{
    return input_dispatcher(
        [this]()
        {
            std::chrono::milliseconds const key_repeat_timeout{500};
            std::chrono::milliseconds const key_repeat_delay{50};

            auto const options = the_options();
            // lp:1675357: Disable generation of key repeat events on nested servers
            auto enable_repeat = options->get<bool>(options::enable_key_repeat_opt);

            return std::make_shared<mi::KeyRepeatDispatcher>(
                the_event_filter_chain_dispatcher(), the_main_loop(), the_cookie_authority(),
                enable_repeat, key_repeat_timeout, key_repeat_delay, false);
        });
}

std::shared_ptr<mi::CursorListener>
mir::DefaultServerConfiguration::the_cursor_listener()
{
    return cursor_listener(
        [this]() -> std::shared_ptr<mi::CursorListener>
        {
            return wrap_cursor_listener(std::make_shared<mi::CursorController>(
                    the_input_scene(),
                    the_cursor(),
                    the_default_cursor_image()));
        });

}

std::shared_ptr<mi::CursorListener>
mir::DefaultServerConfiguration::wrap_cursor_listener(
    std::shared_ptr<mi::CursorListener> const& wrapped)
{
    return wrapped;
}

std::shared_ptr<mi::TouchVisualizer>
mir::DefaultServerConfiguration::the_touch_visualizer()
{
    return touch_visualizer(
        [this]() -> std::shared_ptr<mi::TouchVisualizer>
        {
            auto visualizer = std::make_shared<mi::TouchspotController>(the_buffer_allocator(),
                the_input_scene());

            // The visualizer is disabled by default and can be enabled statically via
            // the MIR_SERVER_ENABLE_TOUCHSPOTS option. In the USC/unity8/autopilot case
            // it will be toggled at runtime via com.canonical.Unity.Screen DBus interface
            if (the_options()->is_set(options::touchspots_opt))
            {
                visualizer->enable();
            }
            
            return visualizer;
        });
}

std::shared_ptr<mg::CursorImage>
mir::DefaultServerConfiguration::the_default_cursor_image()
{
    return default_cursor_image(
        [this]()
        {
            return the_cursor_images()->image(mir_default_cursor_name, mi::default_cursor_size);
        });
}

std::shared_ptr<mi::CursorImages>
mir::DefaultServerConfiguration::the_cursor_images()
{
    return cursor_images(
        []() -> std::shared_ptr<mi::CursorImages>
        {
            return std::make_shared<mi::BuiltinCursorImages>();
        });
}

std::shared_ptr<mi::InputManager>
mir::DefaultServerConfiguration::the_input_manager()
{
    return input_manager(
        [this]() -> std::shared_ptr<mi::InputManager>
        {
            auto const options = the_options();
            bool input_opt = options->get<bool>(options::enable_input_opt);

            if (!input_opt)
            {
                return std::make_shared<mi::NullInputManager>();
            }
            else
            {
                auto const emergency_cleanup = the_emergency_cleanup();
                auto const device_registry = the_input_device_registry();
                auto const input_report = the_input_report();

                // Check whether any of the already-loaded platforms are input platforms;
                // if they are (eg: X11 or wayland), use them
                mir::UniqueModulePtr<mi::Platform> platform;
                for (auto const& module : the_platform_libaries())
                {
                    platform = mi::input_platform_from_graphics_module(
                        *module,
                        *options,
                        emergency_cleanup,
                        device_registry,
                        the_console_services(),
                        input_report);

                    if (platform)
                    {
                        break;
                    }
                }

                // otherwise (usually) we probe for it
                if (!platform)
                {
                    platform = probe_input_platforms(
                        *options,
                        emergency_cleanup,
                        device_registry,
                        the_console_services(),
                        input_report,
                        the_platform_libaries(),
                        *the_shared_library_prober_report());
                }

                return std::make_shared<mi::DefaultInputManager>(the_input_reading_multiplexer(), std::move(platform));
            }
        }
    );
}

std::shared_ptr<mir::dispatch::MultiplexingDispatchable>
mir::DefaultServerConfiguration::the_input_reading_multiplexer()
{
    return input_reading_multiplexer(
        []() -> std::shared_ptr<mir::dispatch::MultiplexingDispatchable>
        {
            return std::make_shared<mir::dispatch::MultiplexingDispatchable>();
        }
    );
}

std::shared_ptr<mi::Seat> mir::DefaultServerConfiguration::the_seat()
{
    return seat(
        [this]()
        {
            return std::make_shared<mi::BasicSeat>(
                    the_input_dispatcher(),
                    the_touch_visualizer(),
                    the_cursor_listener(),
                    the_display_configuration_observer_registrar(),
                    the_key_mapper(),
                    the_clock(),
                    the_seat_observer());
        });
}

std::shared_ptr<mi::InputDeviceRegistry> mir::DefaultServerConfiguration::the_input_device_registry()
{
    return the_default_input_device_hub();
}

std::shared_ptr<mi::InputDeviceHub> mir::DefaultServerConfiguration::the_input_device_hub()
{
    return input_device_hub(
        [this]()
        {
            return std::make_shared<mi::ExternalInputDeviceHub>(the_default_input_device_hub(),
               the_main_loop());
        });
}

std::shared_ptr<mi::DefaultInputDeviceHub> mir::DefaultServerConfiguration::the_default_input_device_hub()
{
   return default_input_device_hub(
       [this]()
       {
           auto input_dispatcher = the_input_dispatcher();
           auto key_repeater = std::dynamic_pointer_cast<mi::KeyRepeatDispatcher>(input_dispatcher);
           auto hub = std::make_shared<mi::DefaultInputDeviceHub>(
               the_seat(),
               the_input_reading_multiplexer(),
               the_clock(),
               the_cookie_authority(),
               the_key_mapper(),
               the_server_status_listener());

           // lp:1675357: KeyRepeatDispatcher must be informed about removed input devices, otherwise
           // pressed keys get repeated indefinitely
           if (key_repeater)
               key_repeater->set_input_device_hub(hub);
           return hub;
       });
}

std::shared_ptr<mi::KeyMapper> mir::DefaultServerConfiguration::the_key_mapper()
{
    return key_mapper(
       []()
       {
           return std::make_shared<mi::receiver::XKBMapper>();
       });
}

std::shared_ptr<mi::SeatObserver> mir::DefaultServerConfiguration::the_seat_observer()
{
    return seat_observer_multiplexer(
        [default_executor = the_main_loop()]()
        {
            return std::make_shared<mi::SeatObserverMultiplexer>(default_executor);
        });
}

std::shared_ptr<mir::ObserverRegistrar<mi::SeatObserver>>
mir::DefaultServerConfiguration::the_seat_observer_registrar()
{
    return seat_observer_multiplexer(
        [default_executor = the_main_loop()]()
        {
            return std::make_shared<mi::SeatObserverMultiplexer>(default_executor);
        });
}
