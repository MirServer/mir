/*
 * Copyright © 2016 Canonical Ltd.
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

#ifndef MIR_INPUT_KEY_MAPPER_H_
#define MIR_INPUT_KEY_MAPPER_H_

#include "mir_toolkit/client_types.h"
#include "mir_toolkit/event.h"

#include <vector>
#include <memory>

struct MirKeyEvent;

namespace mir
{
namespace input
{
class Keymap;

/**
 * The key mapping interface KeyMapper allows configuring a key map for each device individually or a single
 * key map shared by all devices.
 *
 * The key mapping tables can either be provided as xkbcommon text buffers in the XKB_KEYMAP_FORMAT_TEXT_V1 format, or
 * as xkbcommon configuration tuples using the mir::input::Keymap structure.
 */
class KeyMapper
{
public:
    KeyMapper() = default;
    virtual ~KeyMapper() = default;

    /// Update the key state of device \a id, with the given sequence of pressed scan codes.
    virtual void set_key_state(MirInputDeviceId id, std::vector<uint32_t> const& key_state) = 0;

    /**
     * Set a keymap for the device \a id
     */
    virtual void set_keymap_for_device(MirInputDeviceId id, std::shared_ptr<Keymap> map) = 0;

    /**
     * Remove the specific keymap defined for device identified via the \a id.
     *
     * After this call key codes in events processed for device \a id will not be evaluated.
     */
    virtual void clear_keymap_for_device(MirInputDeviceId id) = 0;

    /**
     * Set a default keymap for all devices.
     */
    virtual void set_keymap_for_all_devices(std::shared_ptr<Keymap> map) = 0;

    /*
     * Remove all keymap configurations
     *
     * After this call no key code will be evaluated.
     */
    virtual void clear_all_keymaps() = 0;

    /**
     * Map the given event based on the key maps configured.
     *
     * This includes mapping scan codes in key events onto the respective key code, and also replacing modifier
     * masks in input events with the modifier mask evaluated by this Keymapper.
     */
    virtual void map_event(MirEvent& event) = 0;
    virtual MirInputEventModifiers modifiers() const = 0;
    virtual MirInputEventModifiers device_modifiers(MirInputDeviceId id) const = 0;

protected:
    KeyMapper(KeyMapper const&) = delete;
    KeyMapper& operator=(KeyMapper const&) = delete;
};

}
}

#endif
