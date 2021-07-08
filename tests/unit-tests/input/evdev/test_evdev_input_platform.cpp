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

#include "src/platforms/evdev/platform.h"
#include "src/server/report/null_report_factory.h"
#include "mir/console_services.h"

#include "mir/input/input_device_registry.h"
#include "mir/dispatch/dispatchable.h"

#include "mir/udev/wrapper.h"
#include "mir_test_framework/udev_environment.h"
#include "mir/test/fake_shared.h"
#include "mir/test/doubles/mock_libinput.h"
#include "mir/test/doubles/mock_udev.h"
#include "mir/test/doubles/stub_console_services.h"
#include "mir/test/fd_utils.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <umockdev.h>
#include <memory>
#include <vector>
#include <initializer_list>
#include <mir_test_framework/libinput_environment.h>

namespace mi = mir::input;
namespace mie = mi::evdev;
namespace mr = mir::report;
namespace mu = mir::udev;
namespace mt = mir::test;
namespace mtd = mt::doubles;
namespace mtf = mir_test_framework;

using namespace ::testing;
namespace
{

struct MockInputDeviceRegistry : public mi::InputDeviceRegistry
{
    MOCK_METHOD1(add_device, std::shared_ptr<mi::Device>(std::shared_ptr<mi::InputDevice> const&));
    MOCK_METHOD1(remove_device, void(std::shared_ptr<mi::InputDevice> const&));
};

std::shared_ptr<udev_device> device_for_path(char const* devnode)
{
    auto ctx = std::make_shared<mu::Context>();
    mu::Enumerator devices{ctx};

    devices.scan_devices();

    for (auto const& device : devices)
    {
        if (device.devnode() && strcmp(device.devnode(), devnode) == 0)
        {
            return device.as_raw();
        }
    }

    BOOST_THROW_EXCEPTION((
                              std::runtime_error{
                                  std::string{"Failed to find udev device for "} + devnode}));
}

struct EvdevInputPlatform : public ::testing::TestWithParam<std::string>
{
    testing::NiceMock<MockInputDeviceRegistry> mock_registry;
    mtf::UdevEnvironment udev;
    testing::NiceMock<mtd::MockLibInput> li_mock;

    libinput* const fake_context{reinterpret_cast<libinput*>(0xaabbcc)};

    int device_count{0};
    libinput_device* get_unique_device_ptr()
    {
        return reinterpret_cast<libinput_device*>(++device_count);
    }

    int device_group_count{0};
    libinput_device_group* get_unique_device_group_ptr()
    {
        return reinterpret_cast<libinput_device_group*>(++device_group_count);
    }

    EvdevInputPlatform()
    {
        ON_CALL(li_mock, libinput_path_create_context(_,_))
            .WillByDefault(Return(fake_context));

        ON_CALL(li_mock, libinput_path_add_device(fake_context, _))
            .WillByDefault(
                WithArgs<1>(
                    Invoke(
                        [&](char const* path)
                        {
                            auto const device = get_unique_device_ptr();
                            li_mock.setup_device(
                                device,
                                get_unique_device_group_ptr(),
                                device_for_path(path),
                                "Hello",
                                "SYSNAME",
                                33,
                                44);
                            li_mock.setup_device_add_event(device);
                            return device;
                        })));

        ON_CALL(li_mock, libinput_path_remove_device(_))
            .WillByDefault(
                Invoke(
                    [this](auto device)
                    {
                        li_mock.setup_device_remove_event(device);
                    }));

    }

    auto create_input_platform()
    {
        auto ctx = std::make_unique<mu::Context>();
        return std::make_unique<mie::Platform>(
            mt::fake_shared(mock_registry),
            mr::null_input_report(),
            std::move(ctx),
            std::make_shared<mtd::StubConsoleServices>());
    }

    void remove_all_devices()
    {
        auto ctx = std::make_shared<mir::udev::Context>();

        // We're removing devices while we're enumerating them,
        // so we need to be careful to not attempt to remove an
        // already-removed device.
        bool device_removed = true;
        while (device_removed)
        {
            device_removed = false;
            mir::udev::Enumerator devices{ctx};
            devices.scan_devices();
            if (devices.begin() != devices.end())
            {
                udev.remove_device(devices.begin()->syspath());
                device_removed = true;
            }
        }
    }
};

void run_dispatchable(mir::input::Platform& platform)
{
    while (mt::fd_is_readable(platform.dispatchable()->watch_fd()))
    {
        platform.dispatchable()->dispatch(mir::dispatch::FdEvent::readable);
    }
}
}

TEST_P(EvdevInputPlatform, scans_on_start)
{
    udev.add_standard_device(GetParam());

    using namespace ::testing;
    auto platform = create_input_platform();

    EXPECT_CALL(mock_registry, add_device(_));

    platform->start();
    run_dispatchable(*platform);
}

TEST_P(EvdevInputPlatform, detects_on_hotplug)
{
    using namespace ::testing;
    auto platform = create_input_platform();

    EXPECT_CALL(mock_registry, add_device(_));

    platform->start();
    udev.add_standard_device(GetParam());

    run_dispatchable(*platform);
}

TEST_P(EvdevInputPlatform, detects_hot_removal)
{
    using namespace ::testing;
    auto platform = create_input_platform();


    EXPECT_CALL(mock_registry, add_device(_));
    EXPECT_CALL(mock_registry, remove_device(_));

    udev.add_standard_device(GetParam());

    platform->start();

    run_dispatchable(*platform);
    remove_all_devices();
    run_dispatchable(*platform);
}

TEST_P(EvdevInputPlatform, removes_devices_on_stop)
{
    using namespace ::testing;
    auto platform = create_input_platform();

    udev.add_standard_device(GetParam());
    platform->start();

    run_dispatchable(*platform);
    EXPECT_CALL(mock_registry, remove_device(_));
    platform->stop();
}

INSTANTIATE_TEST_SUITE_P(DeviceHandling,
                        EvdevInputPlatform,
                        ::testing::Values(
                            "synaptics-touchpad",
                            "usb-keyboard",
                            "usb-mouse",
                            "mt-screen-detection",
                            "bluetooth-magic-trackpad"));

TEST_F(EvdevInputPlatform, register_ungrouped_devices)
{
    auto platform = create_input_platform();
    EXPECT_CALL(mock_registry, add_device(_)).Times(2);

    platform->start();

    udev.add_standard_device("synaptics-touchpad");
    udev.add_standard_device("usb-keyboard");
    run_dispatchable(*platform);
}

TEST_F(EvdevInputPlatform, devices_from_same_group)
{
    auto platform = create_input_platform();

    EXPECT_CALL(mock_registry, add_device(_)).Times(2);

    ON_CALL(li_mock, libinput_path_add_device(fake_context, _))
        .WillByDefault(
            WithArgs<1>(
                Invoke(
                    [&](char const* path)
                    {
                        auto device = get_unique_device_ptr();
                        li_mock.setup_device(
                            device,
                            reinterpret_cast<libinput_device_group*>(0xaabbbb),
                            device_for_path(path),
                            "Hello",
                            "SYSNAME",
                            33,
                            44);
                        li_mock.setup_device_add_event(device);
                        return device;
                    })));

    platform->start();

    udev.add_standard_device("synaptics-touchpad");
    udev.add_standard_device("usb-keyboard");
    run_dispatchable(*platform);
}

TEST_F(EvdevInputPlatform, creates_new_context_on_resume)
{
    using namespace ::testing;
    auto platform = create_input_platform();

    platform->start();
    platform->stop();

    EXPECT_CALL(li_mock, libinput_path_create_context(_,_));
    platform->start();
}
