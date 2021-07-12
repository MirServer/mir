/*
 * Copyright © 2014-2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by:
 *   Andreas Pokorny <andreas.pokorny@canonical.com>
 */

#ifndef MIR_INPUT_INPUT_DEVICE_REGISTRY_H_
#define MIR_INPUT_INPUT_DEVICE_REGISTRY_H_

#include <memory>

namespace mir
{
namespace input
{
class Device;
class InputDevice;

class InputDeviceRegistry
{
public:
    InputDeviceRegistry() = default;
    virtual ~InputDeviceRegistry() = default;

    virtual auto add_device(std::shared_ptr<InputDevice> const& device) -> std::shared_ptr<Device> = 0;
    virtual void remove_device(std::shared_ptr<InputDevice> const& device) = 0;
protected:
    InputDeviceRegistry(InputDeviceRegistry const&) = delete;
    InputDeviceRegistry& operator=(InputDeviceRegistry const&) = delete;

};

}
}

#endif
