/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranties of MERCHANTABILITY,
 * SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef MIROIL_INPUT_DEVICE_OBSERVER_H
#define MIROIL_INPUT_DEVICE_OBSERVER_H

#include <miroil/input_device.h>

#include <memory>
#include <string>

namespace miroil
{
class InputDeviceObserver
{
public:
    InputDeviceObserver() = default;
    InputDeviceObserver(InputDeviceObserver const&) = delete;
    InputDeviceObserver& operator=(InputDeviceObserver const&) = delete;    
    virtual ~InputDeviceObserver();
    
    virtual void device_added(miroil::InputDevice device) = 0;
    virtual void device_removed(miroil::InputDevice device) = 0;
};
    
}

#endif //MIROIL_INPUT_DEVICE_OBSERVER_H
