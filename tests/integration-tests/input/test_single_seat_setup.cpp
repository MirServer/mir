/*
 * Copyright © 2015-2019 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or 3 as
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
 * Authored by: Andreas Pokorny <andreas.pokorny@canonical.com>
 */

#include "src/server/input/default_input_device_hub.h"
#include "src/server/input/basic_seat.h"
#include "src/server/input/config_changer.h"
#include "src/server/scene/broadcasting_session_event_sink.h"

#include "mir/test/doubles/mock_input_device.h"
#include "mir/test/doubles/mock_input_device_observer.h"
#include "mir/test/doubles/mock_input_dispatcher.h"
#include "mir/test/doubles/mock_touch_visualizer.h"
#include "mir/test/doubles/mock_cursor_listener.h"
#include "mir/test/doubles/mock_input_manager.h"
#include "mir/test/doubles/mock_seat_report.h"
#include "mir/test/doubles/mock_server_status_listener.h"
#include "mir/test/doubles/mock_scene_session.h"
#include "mir/test/doubles/triggered_main_loop.h"
#include "mir/test/event_matchers.h"
#include "mir/test/doubles/advanceable_clock.h"
#include "mir/test/fake_shared.h"
#include "mir/test/doubles/stub_display_configuration.h"

#include "mir/dispatch/multiplexing_dispatchable.h"
#include "mir/cookie/authority.h"
#include "mir/graphics/buffer.h"
#include "mir/graphics/display_configuration_observer.h"
#include "mir/scene/session_container.h"

#include "mir/input/device.h"
#include "mir/input/xkb_mapper.h"
#include "mir/input/device_capability.h"
#include "mir/input/mir_pointer_config.h"
#include "mir/input/mir_touchpad_config.h"
#include "mir/input/touch_visualizer.h"
#include "mir/input/input_device_info.h"
#include "mir/geometry/rectangles.h"
#include "mir/test/input_config_matchers.h"
#include "mir/test/fd_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace mt = mir::test;
namespace mtd = mt::doubles;
namespace mi = mir::input;
namespace ms = mir::scene;
namespace geom = mir::geometry;
using namespace std::literals::chrono_literals;
using namespace ::testing;

namespace mir
{
namespace input
{
void PrintTo(mir::input::InputDeviceInfo const &info, ::std::ostream* out)
{
    *out << info.name << " " << info.unique_id << " " << info.capabilities.value();
}
}
}

namespace
{

MATCHER_P(DeviceMatches, device_info, "")
{
    return arg.name() == device_info.name &&
        arg.unique_id() == device_info.unique_id;
}

struct FakeDisplayConfigurationObserverRegistrar : mir::ObserverRegistrar<mir::graphics::DisplayConfigurationObserver>
{
    using Observer = mir::graphics::DisplayConfigurationObserver;
    mtd::StubDisplayConfig output{{geom::Rectangle{{0, 0}, {100, 100}}}};
    void register_interest(std::weak_ptr<Observer> const& obs) override
    {
        observer = obs;
        auto o = observer.lock();
        o->initial_configuration(mt::fake_shared(output));
    }
    void register_interest(
        std::weak_ptr<Observer> const& obs,
        mir::Executor&) override
    {
        observer = obs;
    }
    void unregister_interest(Observer const&) override
    {
        observer.reset();
    }
    void update_output(geom::Size const& output_size)
    {
        output.outputs[0].modes[0].size = output_size;
        auto o = observer.lock();
        o->configuration_applied(mt::fake_shared(output));
    }
    std::weak_ptr<Observer> observer;
};
struct SingleSeatInputDeviceHubSetup : ::testing::Test
{
    mtd::TriggeredMainLoop observer_loop;
    NiceMock<mtd::MockInputDispatcher> mock_dispatcher;
    std::shared_ptr<mir::cookie::Authority> cookie_authority = mir::cookie::Authority::create();
    NiceMock<mtd::MockCursorListener> mock_cursor_listener;
    NiceMock<mtd::MockTouchVisualizer> mock_visualizer;
    NiceMock<mtd::MockSeatObserver> mock_seat_observer;
    NiceMock<mtd::MockServerStatusListener> mock_status_listener;
    mi::receiver::XKBMapper key_mapper;
    mir::dispatch::MultiplexingDispatchable multiplexer;
    mtd::AdvanceableClock clock;
    mtd::MockInputManager mock_input_manager;
    ms::SessionContainer session_container;
    ms::BroadcastingSessionEventSink session_event_sink;
    FakeDisplayConfigurationObserverRegistrar display_config;
    mi::BasicSeat seat{mt::fake_shared(mock_dispatcher),      mt::fake_shared(mock_visualizer),
                       mt::fake_shared(mock_cursor_listener), mt::fake_shared(display_config),
                       mt::fake_shared(key_mapper),           mt::fake_shared(clock),
                       mt::fake_shared(mock_seat_observer)};
    mi::DefaultInputDeviceHub hub{
        mt::fake_shared(seat),
        mt::fake_shared(multiplexer),
        mt::fake_shared(clock),
        cookie_authority,
        mt::fake_shared(key_mapper),
        mt::fake_shared(mock_status_listener)};
    NiceMock<mtd::MockInputDeviceObserver> mock_observer;
    mi::ConfigChanger changer{
        mt::fake_shared(mock_input_manager),
            mt::fake_shared(hub),
            mt::fake_shared(session_container),
            mt::fake_shared(session_event_sink),
            mt::fake_shared(hub)
    };

    mi::DeviceCapabilities const keyboard_caps = mi::DeviceCapability::keyboard | mi::DeviceCapability::alpha_numeric;
    mi::DeviceCapabilities const touchpad_caps = mi::DeviceCapability::touchpad | mi::DeviceCapability::pointer;
    NiceMock<mtd::MockInputDevice> device{"device","dev-1", mi::DeviceCapability::unknown};
    NiceMock<mtd::MockInputDevice> another_device{"another_device","dev-2", keyboard_caps};
    NiceMock<mtd::MockInputDevice> third_device{"third_device","dev-3", keyboard_caps};
    NiceMock<mtd::MockInputDevice> touchpad{"touchpad", "dev-4", touchpad_caps};

    std::chrono::nanoseconds arbitrary_timestamp;

    void SetUp()
    {
        // execute registration of ConfigChanger
        expect_and_execute_multiplexer();
    }

    void expect_and_execute_multiplexer(int count = 1)
    {
        for (int i = 0; i != count; ++i)
        {
            mt::fd_becomes_readable(multiplexer.watch_fd(), 5s);
            multiplexer.dispatch(mir::dispatch::FdEvent::readable);
        }
    }

    void capture_input_sink(NiceMock<mtd::MockInputDevice>& dev, mi::InputSink*& sink, mi::EventBuilder*& builder)
    {
        ON_CALL(dev,start(_,_))
            .WillByDefault(Invoke([&sink,&builder](mi::InputSink* input_sink, mi::EventBuilder* event_builder)
                                  {
                                      sink = input_sink;
                                      builder = event_builder;
                                  }
                                 ));
    }
};

}

TEST_F(SingleSeatInputDeviceHubSetup, input_sink_posts_events_to_input_dispatcher)
{
    mi::InputSink* sink;
    mi::EventBuilder* builder;
    std::shared_ptr<mi::Device> handle;

    capture_input_sink(device, sink, builder);

    EXPECT_CALL(mock_observer, device_added(_))
        .WillOnce(SaveArg<0>(&handle));

    hub.add_observer(mt::fake_shared(mock_observer));
    expect_and_execute_multiplexer(1);
    hub.add_device(mt::fake_shared(device));

    auto event = builder->key_event(arbitrary_timestamp, mir_keyboard_action_down, 0,
                                    KEY_A);

    EXPECT_CALL(mock_dispatcher, dispatch(AllOf(mt::InputDeviceIdMatches(handle->id()), mt::MirKeyboardEventMatches(event.get()))));

    sink->handle_input(std::move(event));
}

TEST_F(SingleSeatInputDeviceHubSetup, forwards_touch_spots_to_visualizer)
{
    mi::InputSink* sink;
    mi::EventBuilder* builder;

    capture_input_sink(device, sink, builder);

    hub.add_device(mt::fake_shared(device));

    auto touch_event_1 = builder->touch_event(
        arbitrary_timestamp,
        {{0, mir_touch_action_down, mir_touch_tooltype_finger, 21.0f, 34.0f, 50.0f, 15.0f, 5.0f, 4.0f}});

    auto touch_event_2 = builder->touch_event(
        arbitrary_timestamp,
        {{0, mir_touch_action_change, mir_touch_tooltype_finger, 24.0f, 34.0f, 50.0f, 15.0f, 5.0f, 4.0f},
         {1, mir_touch_action_down, mir_touch_tooltype_finger, 60.0f, 34.0f, 50.0f, 15.0f, 5.0f, 4.0f}});

    auto touch_event_3 = builder->touch_event(
        arbitrary_timestamp,
        {{0, mir_touch_action_up, mir_touch_tooltype_finger, 24.0f, 34.0f, 50.0f, 15.0f, 5.0f, 4.0f},
         {1, mir_touch_action_change, mir_touch_tooltype_finger, 70.0f, 30.0f, 50.0f, 15.0f, 5.0f, 4.0f}});

    auto touch_event_4 = builder->touch_event(
        arbitrary_timestamp,
        {{1, mir_touch_action_up, mir_touch_tooltype_finger, 70.0f, 35.0f, 50.0f, 15.0f, 5.0f, 4.0f}});


    using Spot = mi::TouchVisualizer::Spot;

    InSequence seq;
    EXPECT_CALL(mock_visualizer, visualize_touches(ElementsAreArray({Spot{{21,34}, 50}})));
    EXPECT_CALL(mock_visualizer, visualize_touches(ElementsAreArray({Spot{{24,34}, 50}, Spot{{60,34}, 50}})));
    EXPECT_CALL(mock_visualizer, visualize_touches(ElementsAreArray({Spot{{70,30}, 50}})));
    EXPECT_CALL(mock_visualizer, visualize_touches(ElementsAreArray(std::vector<Spot>())));

    sink->handle_input(std::move(touch_event_1));
    sink->handle_input(std::move(touch_event_2));
    sink->handle_input(std::move(touch_event_3));
    sink->handle_input(std::move(touch_event_4));
}

TEST_F(SingleSeatInputDeviceHubSetup, tracks_pointer_position)
{
    geom::Point first{10,10}, second{20,20}, third{10,30};
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(first.x.as_int(), first.y.as_int()));
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(second.x.as_int(), second.y.as_int()));
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(third.x.as_int(), third.y.as_int()));

    mi::InputSink* sink;
    mi::EventBuilder* builder;
    capture_input_sink(device, sink, builder);

    hub.add_device(mt::fake_shared(device));
    sink->handle_input(
        builder->pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0.0f, 0.0f, 10.0f, 10.0f));
    sink->handle_input(
        builder->pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0.0f, 0.0f, 10.0f, 10.0f));
    sink->handle_input(
        builder->pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0.0f, 0.0f, -10.0f, 10.0f));
}

TEST_F(SingleSeatInputDeviceHubSetup, confines_pointer_movement)
{
    geom::Point confined_pos{10, 18};
    display_config.update_output(geom::Size{confined_pos.x.as_int() + 1, confined_pos.y.as_int() + 1});

    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(confined_pos.x.as_int(), confined_pos.y.as_int())).Times(2);

    mi::InputSink* sink;
    mi::EventBuilder* builder;
    capture_input_sink(device, sink, builder);
    hub.add_device(mt::fake_shared(device));

    sink->handle_input(
        builder->pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0.0f, 0.0f, 10.0f, 20.0f));
    sink->handle_input(
        builder->pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0.0f, 0.0f, 10.0f, 10.0f));
}

TEST_F(SingleSeatInputDeviceHubSetup, forwards_pointer_updates_to_cursor_listener)
{
    auto move_x = 12.0f, move_y = 14.0f;

    mi::InputSink* sink;
    mi::EventBuilder* builder;
    capture_input_sink(device, sink, builder);
    hub.add_device(mt::fake_shared(device));

    auto event = builder->pointer_event(0ns, mir_pointer_action_motion, 0, 0.0f, 0.0f, move_x, move_y);

    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(move_x, move_y)).Times(1);

    sink->handle_input(std::move(event));
}

TEST_F(SingleSeatInputDeviceHubSetup, forwards_pointer_settings_to_input_device)
{
    std::shared_ptr<mi::Device> dev;
    ON_CALL(mock_observer, device_added(_)).WillByDefault(SaveArg<0>(&dev));

    hub.add_observer(mt::fake_shared(mock_observer));
    expect_and_execute_multiplexer();
    hub.add_device(mt::fake_shared(touchpad));

    EXPECT_CALL(touchpad, apply_settings(Matcher<mi::PointerSettings const&>(_)));

    auto conf = dev->pointer_configuration();
    dev->apply_pointer_configuration(conf.value());
    multiplexer.dispatch(mir::dispatch::FdEvent::readable);
}

TEST_F(SingleSeatInputDeviceHubSetup, forwards_touchpad_settings_to_input_device)
{
    std::shared_ptr<mi::Device> dev;
    ON_CALL(mock_observer, device_added(_)).WillByDefault(SaveArg<0>(&dev));

    hub.add_observer(mt::fake_shared(mock_observer));
    expect_and_execute_multiplexer();
    hub.add_device(mt::fake_shared(touchpad));

    EXPECT_CALL(touchpad, apply_settings(Matcher<mi::TouchpadSettings const&>(_)));

    auto conf = dev->touchpad_configuration();
    dev->apply_touchpad_configuration(conf.value());
    multiplexer.dispatch(mir::dispatch::FdEvent::readable);
}

TEST_F(SingleSeatInputDeviceHubSetup, input_sink_tracks_modifier)
{
    mi::InputSink* key_board_sink;
    mi::EventBuilder* key_event_builder;
    std::shared_ptr<mi::Device> key_handle;

    capture_input_sink(another_device, key_board_sink, key_event_builder);

    InSequence seq;
    EXPECT_CALL(mock_observer,device_added(_))
        .WillOnce(SaveArg<0>(&key_handle));

    hub.add_observer(mt::fake_shared(mock_observer));
    expect_and_execute_multiplexer();
    hub.add_device(mt::fake_shared(another_device));


    const MirInputEventModifiers shift_left = mir_input_event_modifier_shift_left | mir_input_event_modifier_shift;
    auto shift_down =
        key_event_builder->key_event(arbitrary_timestamp, mir_keyboard_action_down, 0, KEY_LEFTSHIFT);
    auto shift_up =
        key_event_builder->key_event(arbitrary_timestamp, mir_keyboard_action_up, 0, KEY_LEFTSHIFT);

    EXPECT_CALL(mock_dispatcher, dispatch(mt::KeyWithModifiers(shift_left)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::KeyWithModifiers(mir_input_event_modifier_none)));

    key_board_sink->handle_input(std::move(shift_down));
    key_board_sink->handle_input(std::move(shift_up));
}

TEST_F(SingleSeatInputDeviceHubSetup, input_sink_unifies_modifier_state_accross_devices)
{
    mi::InputSink* mouse_sink;
    mi::EventBuilder* mouse_event_builder;
    mi::InputSink* key_board_sink;
    mi::EventBuilder* key_event_builder;
    std::shared_ptr<mi::Device> mouse_handle;
    std::shared_ptr<mi::Device> key_handle;

    capture_input_sink(device, mouse_sink, mouse_event_builder);
    capture_input_sink(another_device, key_board_sink, key_event_builder);

    InSequence seq;
    EXPECT_CALL(mock_observer,device_added(_))
        .WillOnce(SaveArg<0>(&mouse_handle));
    EXPECT_CALL(mock_observer,device_added(_))
        .WillOnce(SaveArg<0>(&key_handle));

    hub.add_observer(mt::fake_shared(mock_observer));
    expect_and_execute_multiplexer();
    hub.add_device(mt::fake_shared(device));
    hub.add_device(mt::fake_shared(another_device));

    const MirInputEventModifiers r_alt_modifier = mir_input_event_modifier_alt_right | mir_input_event_modifier_alt;
    auto key =
        key_event_builder->key_event(arbitrary_timestamp, mir_keyboard_action_down, 0, KEY_RIGHTALT);
    auto motion =
        mouse_event_builder->pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0, 0, 12, 40);

    EXPECT_CALL(mock_dispatcher, dispatch(mt::KeyWithModifiers(r_alt_modifier)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::PointerEventWithModifiers(r_alt_modifier)));

    key_board_sink->handle_input(std::move(key));
    mouse_sink->handle_input(std::move(motion));

    EXPECT_THAT(key_handle->id(), Ne(mouse_handle->id()));
}

TEST_F(SingleSeatInputDeviceHubSetup, input_sink_reduces_modifier_state_accross_devices)
{
    mi::InputSink* mouse_sink;
    mi::EventBuilder* mouse_event_builder;
    mi::InputSink* key_board_sink_1;
    mi::EventBuilder* key_event_builder_1;
    mi::InputSink* key_board_sink_2;
    mi::EventBuilder* key_event_builder_2;
    std::shared_ptr<mi::Device> mouse_handle;
    std::shared_ptr<mi::Device> key_handle_1;
    std::shared_ptr<mi::Device> key_handle_2;

    capture_input_sink(device, mouse_sink, mouse_event_builder);
    capture_input_sink(another_device, key_board_sink_1, key_event_builder_1);
    capture_input_sink(third_device, key_board_sink_2, key_event_builder_2);

    InSequence seq;
    EXPECT_CALL(mock_observer, device_added(_))
        .WillOnce(SaveArg<0>(&mouse_handle));
    EXPECT_CALL(mock_observer, device_added(_))
        .WillOnce(SaveArg<0>(&key_handle_1));
    EXPECT_CALL(mock_observer, device_added(_))
        .WillOnce(SaveArg<0>(&key_handle_2));

    hub.add_observer(mt::fake_shared(mock_observer));
    expect_and_execute_multiplexer();
    hub.add_device(mt::fake_shared(device));
    hub.add_device(mt::fake_shared(another_device));
    hub.add_device(mt::fake_shared(third_device));

    const MirInputEventModifiers r_alt_modifier = mir_input_event_modifier_alt_right | mir_input_event_modifier_alt;
    const MirInputEventModifiers l_ctrl_modifier = mir_input_event_modifier_ctrl_left | mir_input_event_modifier_ctrl;
    const MirInputEventModifiers combined_modifier = r_alt_modifier | l_ctrl_modifier;

    auto alt_down = key_event_builder_1->key_event(arbitrary_timestamp, mir_keyboard_action_down, 0, KEY_RIGHTALT);
    auto ctrl_down = key_event_builder_2->key_event(arbitrary_timestamp, mir_keyboard_action_down, 0, KEY_LEFTCTRL);
    auto ctrl_up = key_event_builder_2->key_event(arbitrary_timestamp, mir_keyboard_action_up, 0, KEY_LEFTCTRL);

    auto motion_1 =
        mouse_event_builder->pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0, 0, 12, 40);
    auto motion_2 =
        mouse_event_builder->pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0, 0, 18, 10);

    EXPECT_CALL(mock_dispatcher, dispatch(mt::KeyWithModifiers(r_alt_modifier)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::KeyWithModifiers(l_ctrl_modifier)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::PointerEventWithModifiers(combined_modifier)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::KeyWithModifiers(mir_input_event_modifier_none)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::PointerEventWithModifiers(r_alt_modifier)));

    key_board_sink_1->handle_input(std::move(alt_down));
    key_board_sink_2->handle_input(std::move(ctrl_down));
    mouse_sink->handle_input(std::move(motion_1));
    key_board_sink_2->handle_input(std::move(ctrl_up));
    mouse_sink->handle_input(std::move(motion_2));

    EXPECT_THAT(key_handle_1->id(), Ne(key_handle_2->id()));
}

TEST_F(SingleSeatInputDeviceHubSetup, tracks_a_single_cursor_position_from_multiple_pointing_devices)
{
    mi::InputSink* mouse_sink_1;
    mi::EventBuilder* mouse_event_builder_1;
    mi::InputSink* mouse_sink_2;
    mi::EventBuilder* mouse_event_builder_2;

    capture_input_sink(device, mouse_sink_1, mouse_event_builder_1);
    capture_input_sink(another_device, mouse_sink_2, mouse_event_builder_2);

    hub.add_device(mt::fake_shared(device));
    hub.add_device(mt::fake_shared(another_device));

    auto motion_1 =
        mouse_event_builder_1->pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0, 0, 23, 20);
    auto motion_2 =
        mouse_event_builder_2->pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0, 0, 18, -10);
    auto motion_3 =
        mouse_event_builder_2->pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0, 0, 2, 10);

    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(23, 20));
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(41, 10));
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(43, 20));

    mouse_sink_1->handle_input(std::move(motion_1));
    mouse_sink_2->handle_input(std::move(motion_2));
    mouse_sink_1->handle_input(std::move(motion_3));
}

TEST_F(SingleSeatInputDeviceHubSetup, tracks_a_single_button_state_from_multiple_pointing_devices)
{
    int const x = 0, y = 0;
    MirPointerButtons no_buttons = 0;
    mi::InputSink* mouse_sink_1;
    mi::EventBuilder* mouse_event_builder_1;
    mi::InputSink* mouse_sink_2;
    mi::EventBuilder* mouse_event_builder_2;

    capture_input_sink(device, mouse_sink_1, mouse_event_builder_1);
    capture_input_sink(another_device, mouse_sink_2, mouse_event_builder_2);

    hub.add_device(mt::fake_shared(device));
    hub.add_device(mt::fake_shared(another_device));

    auto motion_1 =
        mouse_event_builder_1->pointer_event(arbitrary_timestamp, mir_pointer_action_button_down, mir_pointer_button_primary, 0, 0, 0, 0);
    auto motion_2 =
        mouse_event_builder_2->pointer_event(arbitrary_timestamp, mir_pointer_action_button_down, mir_pointer_button_secondary, 0, 0, 0, 0);
    auto motion_3 =
        mouse_event_builder_1->pointer_event(arbitrary_timestamp, mir_pointer_action_button_up, no_buttons, 0, 0, 0, 0);
    auto motion_4 =
        mouse_event_builder_2->pointer_event(arbitrary_timestamp, mir_pointer_action_button_up, no_buttons, 0, 0, 0, 0);

    InSequence seq;
    EXPECT_CALL(mock_dispatcher, dispatch(mt::ButtonsDown(x, y, mir_pointer_button_primary)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::ButtonsDown(x, y, mir_pointer_button_primary | mir_pointer_button_secondary)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::ButtonsUp(x, y, mir_pointer_button_secondary)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::ButtonsUp(x, y, no_buttons)));

    mouse_sink_1->handle_input(std::move(motion_1));
    mouse_sink_2->handle_input(std::move(motion_2));
    mouse_sink_1->handle_input(std::move(motion_3));
    mouse_sink_1->handle_input(std::move(motion_4));
}

TEST_F(SingleSeatInputDeviceHubSetup, input_device_changes_sent_to_session)
{
    NiceMock<mtd::MockSceneSession> session;
    session_container.insert_session(mt::fake_shared(session));

    EXPECT_CALL(session, send_input_config(UnorderedElementsAre(DeviceMatches(device.get_device_info()))));
    hub.add_device(mt::fake_shared(device));
}

TEST_F(SingleSeatInputDeviceHubSetup, input_device_changes_sent_to_session_multiple_devices)
{
    NiceMock<mtd::MockSceneSession> session;
    session_container.insert_session(mt::fake_shared(session));

    hub.add_device(mt::fake_shared(device));

    EXPECT_CALL(session,
                send_input_config(UnorderedElementsAre(DeviceMatches(device.get_device_info()),
                                                       DeviceMatches(another_device.get_device_info()))));
    hub.add_device(mt::fake_shared(another_device));
}

TEST_F(SingleSeatInputDeviceHubSetup, input_device_changes_sent_to_sink_removal)
{
    hub.add_device(mt::fake_shared(device));
    hub.add_device(mt::fake_shared(another_device));

    NiceMock<mtd::MockSceneSession> session;
    session_container.insert_session(mt::fake_shared(session));
    EXPECT_CALL(session,
                send_input_config(UnorderedElementsAre(DeviceMatches(another_device.get_device_info()))));
    hub.remove_device(mt::fake_shared(device));
}
