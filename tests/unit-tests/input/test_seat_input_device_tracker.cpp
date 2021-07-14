/*
 * Copyright © 2015 Canonical Ltd.
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

#include "src/server/input/seat_input_device_tracker.h"
#include "src/server/input/default_event_builder.h"

#include "mir/input/xkb_mapper.h"
#include "mir/test/doubles/mock_input_device.h"
#include "mir/test/doubles/mock_input_dispatcher.h"
#include "mir/test/doubles/mock_cursor_listener.h"
#include "mir/test/doubles/mock_touch_visualizer.h"
#include "mir/test/doubles/mock_input_seat.h"
#include "mir/test/doubles/mock_seat_report.h"
#include "mir/test/doubles/advanceable_clock.h"
#include "mir/test/event_matchers.h"
#include "mir/test/fake_shared.h"

#include "mir/geometry/rectangles.h"
#include "mir/cookie/authority.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <linux/input.h>

namespace mt = mir::test;
namespace mtd = mt::doubles;
namespace mi = mir::input;
namespace geom = mir::geometry;
namespace
{

template<typename Type>
using Nice = ::testing::NiceMock<Type>;
using namespace ::testing;

struct SeatInputDeviceTracker : ::testing::Test
{
    mi::EventBuilder* builder;
    Nice<mtd::MockInputDispatcher> mock_dispatcher;
    Nice<mtd::MockCursorListener> mock_cursor_listener;
    Nice<mtd::MockTouchVisualizer> mock_visualizer;
    Nice<mtd::MockInputSeat> mock_seat;
    Nice<mtd::MockSeatObserver> mock_seat_report;
    MirInputDeviceId some_device{8712};
    MirInputDeviceId another_device{1246};
    MirInputDeviceId third_device{86};
    std::shared_ptr<mir::cookie::Authority> cookie_factory = mir::cookie::Authority::create();
    mtd::AdvanceableClock clock;

    mi::DefaultEventBuilder some_device_builder{
        some_device,
        mt::fake_shared(clock),
        cookie_factory,
        mt::fake_shared(mock_seat)};
    mi::DefaultEventBuilder another_device_builder{
        another_device,
        mt::fake_shared(clock),
        cookie_factory,
        mt::fake_shared(mock_seat)};
    mi::DefaultEventBuilder third_device_builder{
        third_device,
        mt::fake_shared(clock),
        cookie_factory,
        mt::fake_shared(mock_seat)};
    mi::receiver::XKBMapper mapper;
    mi::SeatInputDeviceTracker tracker{
        mt::fake_shared(mock_dispatcher), mt::fake_shared(mock_visualizer), mt::fake_shared(mock_cursor_listener),
        mt::fake_shared(mapper),          mt::fake_shared(clock),           mt::fake_shared(mock_seat_report)};

    std::chrono::nanoseconds arbitrary_timestamp;
};

}

TEST_F(SeatInputDeviceTracker, throws_on_unknown_device)
{
    EXPECT_CALL(mock_dispatcher, dispatch(_)).Times(0);
    EXPECT_THROW(
        {
            tracker.dispatch(some_device_builder.touch_event(arbitrary_timestamp,{}));
        },
        std::logic_error);
}

TEST_F(SeatInputDeviceTracker, dispatch_posts_to_input_dispatcher)
{
    EXPECT_CALL(mock_dispatcher, dispatch(_));

    tracker.add_device(some_device);
    tracker.dispatch(some_device_builder.touch_event(arbitrary_timestamp, {}));
}

TEST_F(SeatInputDeviceTracker, forwards_touch_spots_to_visualizer)
{
    using Spot = mi::TouchVisualizer::Spot;
    InSequence seq;
    EXPECT_CALL(mock_visualizer, visualize_touches(ElementsAreArray({Spot{{4, 2}, 10}})));
    EXPECT_CALL(mock_visualizer, visualize_touches(ElementsAreArray({Spot{{10, 10}, 30}, Spot{{100, 34}, 0}})));
    EXPECT_CALL(mock_visualizer, visualize_touches(ElementsAreArray({Spot{{70, 10}, 30}})));
    EXPECT_CALL(mock_visualizer, visualize_touches(ElementsAreArray(std::vector<Spot>())));

    tracker.add_device(some_device);

    auto touch_event_1 = some_device_builder.touch_event(
        arbitrary_timestamp,
        {{0, mir_touch_action_down, mir_touch_tooltype_finger, 4.0f, 2.0f, 10.0f, 15.0f, 5.0f, 4.0f}});

    auto touch_event_2 = some_device_builder.touch_event(
        arbitrary_timestamp,
        {{0, mir_touch_action_change, mir_touch_tooltype_finger, 10.0f, 10.0f, 30.0f, 15.0f, 5.0f, 4.0f},
         {1, mir_touch_action_down, mir_touch_tooltype_finger, 100.0f, 34.0f, 0.0f, 15.0f, 5.0f, 4.0f}});

    auto touch_event_3 = some_device_builder.touch_event(
        arbitrary_timestamp,
        {{0, mir_touch_action_up, mir_touch_tooltype_finger, 100.0f, 34.0f, 0.0f, 15.0f, 5.0f, 4.0f},
         {1, mir_touch_action_change, mir_touch_tooltype_finger, 70.0f, 10.0f, 30.0f, 15.0f, 5.0f, 4.0f}});

    auto touch_event_4 = some_device_builder.touch_event(
        arbitrary_timestamp,
        {{1, mir_touch_action_up, mir_touch_tooltype_finger, 70.0f, 10.0f, 30.0f, 15.0f, 5.0f, 4.0f}});

    tracker.dispatch(std::move(touch_event_1));
    tracker.dispatch(std::move(touch_event_2));
    tracker.dispatch(std::move(touch_event_3));
    tracker.dispatch(std::move(touch_event_4));
}

TEST_F(SeatInputDeviceTracker, removal_of_touch_device_removes_spots)
{
    using Spot = mi::TouchVisualizer::Spot;
    InSequence seq;
    EXPECT_CALL(mock_visualizer, visualize_touches(ElementsAreArray({Spot{{4, 2}, 10}})));
    EXPECT_CALL(mock_visualizer, visualize_touches(ElementsAreArray(std::vector<Spot>())));

    tracker.add_device(some_device);

    auto touch_event_1 = some_device_builder.touch_event(
        arbitrary_timestamp,
        {{0, mir_touch_action_down, mir_touch_tooltype_finger, 4.0f, 2.0f, 10.0f, 15.0f, 5.0f, 4.0f}});

    tracker.dispatch(std::move(touch_event_1));
    tracker.remove_device(some_device);
}

TEST_F(SeatInputDeviceTracker, pointer_movement_updates_cursor)
{
    auto const move_x = 20.0f, move_y = 40.0f;
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(move_x, move_y)).Times(1);

    tracker.add_device(some_device);
    tracker.dispatch(some_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0.0f, 0.0f,
                                                        move_x, move_y));
}

TEST_F(SeatInputDeviceTracker, slow_pointer_movement_updates_cursor)
{   // Regression test for LP: #1528109
    float const step = 0.25f;
    float const target = 3.0f;

    for (float x = step; x <= target; x += step)
        EXPECT_CALL(mock_cursor_listener,
                    cursor_moved_to(FloatEq(x), FloatEq(x))).Times(1);

    tracker.add_device(some_device);

    for (float x = step; x <= target; x += step)
        tracker.dispatch(some_device_builder.pointer_event(
            arbitrary_timestamp, mir_pointer_action_motion, 0, 0.0f, 0.0f,
            step, step));
}

TEST_F(SeatInputDeviceTracker, pointer_movement_from_different_devices_change_cursor_position)
{
    InSequence seq;
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(23, 20));
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(41, 10));
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(43, 20));

    tracker.add_device(some_device);
    tracker.add_device(another_device);

    tracker.dispatch(
        some_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0, 0, 23, 20));
    tracker.dispatch(
        another_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0, 0, 18, -10));
    tracker.dispatch(
        another_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0, 0, 2, 10));
}

TEST_F(SeatInputDeviceTracker, tracks_a_single_button_state_for_multiple_pointing_devices)
{
    int const x = 0, y = 0;
    MirPointerButtons no_buttons = 0;
    InSequence seq;
    EXPECT_CALL(mock_dispatcher, dispatch(mt::ButtonsDown(x, y, mir_pointer_button_primary)));
    EXPECT_CALL(mock_dispatcher,
                dispatch(mt::ButtonsDown(x, y, mir_pointer_button_primary | mir_pointer_button_secondary)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::ButtonsUp(x, y, mir_pointer_button_secondary)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::ButtonsUp(x, y, no_buttons)));

    tracker.add_device(some_device);
    tracker.add_device(another_device);

    tracker.dispatch(some_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_button_down,
                                                        mir_pointer_button_primary, 0, 0, 0, 0));
    tracker.dispatch(another_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_button_down,
                                                           mir_pointer_button_secondary, 0, 0, 0, 0));
    tracker.dispatch(
        some_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_button_up, no_buttons, 0, 0, 0, 0));
    tracker.dispatch(another_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_button_up,
                                                           no_buttons, 0, 0, 0, 0));
}

TEST_F(SeatInputDeviceTracker, pointing_device_removal_removes_pressed_button_state)
{
    int const x = 0, y = 0;

    InSequence seq;
    EXPECT_CALL(mock_dispatcher, dispatch(mt::ButtonsDown(x, y, mir_pointer_button_primary)));
    EXPECT_CALL(mock_dispatcher,
                dispatch(mt::ButtonsDown(x, y, mir_pointer_button_primary | mir_pointer_button_secondary)));
    EXPECT_CALL(mock_dispatcher, dispatch(mt::ButtonsDown(x, y, mir_pointer_button_secondary)));

    tracker.add_device(some_device);
    tracker.add_device(another_device);
    tracker.dispatch(some_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_button_down,
                                                        mir_pointer_button_primary, 0, 0, 0, 0));
    tracker.dispatch(another_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_button_down,
                                                           mir_pointer_button_secondary, 0, 0, 0, 0));
    tracker.remove_device(some_device);
    tracker.dispatch(another_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_motion,
                                                           mir_pointer_button_secondary, 0, 0, 0, 0));
}

TEST_F(SeatInputDeviceTracker, inconsistent_key_down_dropped)
{
    tracker.add_device(some_device);
    EXPECT_CALL(mock_dispatcher, dispatch(mt::KeyOfScanCode(KEY_A))).Times(1);

    tracker.dispatch(some_device_builder.key_event(arbitrary_timestamp, mir_keyboard_action_down, 0, KEY_A));
    tracker.dispatch(some_device_builder.key_event(arbitrary_timestamp, mir_keyboard_action_down, 0, KEY_A));
    tracker.dispatch(some_device_builder.key_event(arbitrary_timestamp, mir_keyboard_action_down, 0, KEY_A));
}

TEST_F(SeatInputDeviceTracker, repeat_without_down_dropped)
{
    tracker.add_device(some_device);
    EXPECT_CALL(mock_dispatcher, dispatch(mt::KeyOfScanCode(KEY_A))).Times(0);

    tracker.dispatch(some_device_builder.key_event(arbitrary_timestamp, mir_keyboard_action_repeat, 0, KEY_A));
    tracker.dispatch(some_device_builder.key_event(arbitrary_timestamp, mir_keyboard_action_repeat, 0, KEY_A));
}

TEST_F(SeatInputDeviceTracker, inconsistent_key_up_dropped)
{
    tracker.add_device(some_device);
    EXPECT_CALL(mock_dispatcher, dispatch(mt::KeyOfScanCode(KEY_A))).Times(0);

    tracker.dispatch(some_device_builder.key_event(arbitrary_timestamp, mir_keyboard_action_up, 0, KEY_A));
}

TEST_F(SeatInputDeviceTracker, pointer_confinement_bounds_mouse_inside)
{
    auto const move_x = 20.0f, move_y = 40.0f;
    auto const max_w_h = 100;
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(move_x, move_y)).Times(1);
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(max_w_h - 1, max_w_h - 1)).Times(1);
   
    geom::Rectangle rec{{0, 0}, {max_w_h, max_w_h}}; 
    tracker.set_confinement_regions({rec});
    tracker.add_device(some_device);
    tracker.dispatch(some_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0.0f, 0.0f,
                                                    move_x, move_y));
    tracker.dispatch(some_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0.0f, 0.0f,
                                                    max_w_h * 2, max_w_h * 2));
}

TEST_F(SeatInputDeviceTracker, reset_pointer_confinement_allows_movement_past)
{
    auto const move_x = 20.0f, move_y = 40.0f;
    auto const max_w_h = 100;
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(move_x, move_y)).Times(1);
    EXPECT_CALL(mock_cursor_listener, cursor_moved_to(move_x + max_w_h * 2,
                                                      move_y + max_w_h * 2)).Times(1);

    geom::Rectangle rec{{0, 0}, {max_w_h, max_w_h}}; 
    tracker.set_confinement_regions({rec});
    tracker.add_device(some_device);
    tracker.dispatch(some_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0.0f, 0.0f,
                                                    move_x, move_y));

    tracker.reset_confinement_regions();
    tracker.dispatch(some_device_builder.pointer_event(arbitrary_timestamp, mir_pointer_action_motion, 0, 0.0f, 0.0f,
                                                    max_w_h * 2, max_w_h * 2));
}
