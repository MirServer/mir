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
 * Authored by: Cemil Azizoglu <cemil.azizoglu@canonical.com>
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "src/server/input/default_event_builder.h"
#include "src/platforms/x11/input/input_platform.h"
#include "src/platforms/x11/input/input_device.h"

#include "mir/events/event_private.h"
#include "mir/dispatch/dispatchable.h"
#include "mir_toolkit/event.h"
#include "mir_toolkit/events/input/input_event.h"
#include "mir/test/doubles/mock_input_sink.h"
#include "mir/test/doubles/mock_input_seat.h"
#include "mir/test/doubles/mock_input_device_registry.h"
#include "mir/test/doubles/mock_x11.h"
#include "mir/test/doubles/mock_xkb.h"
#include "mir/test/doubles/mock_x11_resources.h"
#include "mir/test/fake_shared.h"
#include "mir/cookie/authority.h"
#include "mir/test/event_matchers.h"

namespace md = mir::dispatch;
namespace mi = mir::input;
namespace mt = mir::test;
namespace mtd = mt::doubles;

using namespace ::testing;

namespace
{

struct X11PlatformTest : ::testing::Test
{
    NiceMock<mtd::MockInputSink> mock_pointer_sink;
    NiceMock<mtd::MockInputSink> mock_keyboard_sink;
    NiceMock<mtd::MockInputSeat> mock_seat;
    NiceMock<mtd::MockX11> mock_x11;
    NiceMock<mtd::MockXkb> mock_xkb;
    NiceMock<mtd::MockInputDeviceRegistry> mock_registry;
    mir::input::DefaultEventBuilder builder{0, mir::cookie::Authority::create(), mt::fake_shared(mock_seat)};

    mir::input::X::XInputPlatform x11_platform{
        mt::fake_shared(mock_registry),
        std::make_shared<mtd::MockX11Resources>()};

    std::vector<std::shared_ptr<mi::InputDevice>> devices;
    enum class GrabState { FOCUSIN, FOCUSOUT, ENTERNOTIFY, LEAVENOTIFY };
    GrabState grab_state{GrabState::FOCUSIN};

    void capture_devices()
    {
        ON_CALL(mock_registry, add_device(_))
            .WillByDefault(Invoke([this](auto const& dev)
                                  {
                                      devices.push_back(dev);
                                      auto const info = dev->get_device_info();
                                      if (contains(info.capabilities, mi::DeviceCapability::pointer))
                                          dev->start(&mock_pointer_sink, &builder);
                                      else if (contains(info.capabilities, mi::DeviceCapability::keyboard))
                                          dev->start(&mock_keyboard_sink, &builder);
                                      return nullptr;
                                  })
                          );
    }

    void prepare_event_processing()
    {
        capture_devices();
        x11_platform.start();
    }


    void process_input_event()
    {
        x11_platform.dispatchable()->dispatch(md::FdEvent::readable);
    }
};

}

MATCHER(ButtonUpEventWithNoButtonsPressed, "")
{
    auto pev = mt::maybe_pointer_event(mt::to_address(arg));
    return mt::button_event_matches(pev, 0, 0, mir_pointer_action_button_up, 0, true, true, false);
}

TEST_F(X11PlatformTest, registers_two_devices)
{
    EXPECT_CALL(mock_registry, add_device(_)).Times(2);

    x11_platform.start();
}

TEST_F(X11PlatformTest, unregisters_devices_on_stop)
{
    EXPECT_CALL(mock_registry, add_device(_)).Times(2);
    EXPECT_CALL(mock_registry, remove_device(_)).Times(2);

    x11_platform.start();
    x11_platform.stop();
}

TEST_F(X11PlatformTest, registered_devices_mimic_mouse_and_keyboard)
{
    capture_devices();

    x11_platform.start();

    ASSERT_THAT(devices.size(), Eq(2u));

    auto const& first_info = devices[0]->get_device_info();
    auto const& second_info = devices[1]->get_device_info();

    EXPECT_FALSE(first_info.capabilities == second_info.capabilities);
    EXPECT_TRUE(contains(first_info.capabilities | second_info.capabilities,
                         mi::DeviceCapability::pointer));
    EXPECT_TRUE(contains(first_info.capabilities | second_info.capabilities,
                         mi::DeviceCapability::keyboard));

}
