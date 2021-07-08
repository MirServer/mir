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

#include "mir/input/mir_keyboard_config.h"
#include "mir/input/parameter_keymap.h"

#include <ostream>

namespace mi = mir::input;

struct MirKeyboardConfig::Implementation
{
    Implementation()
        : device_keymap{std::make_shared<mi::ParameterKeymap>()}
    {
    }

    Implementation(std::shared_ptr<mi::Keymap> && keymap)
        : device_keymap{std::move(keymap)}
    {
    }

    std::shared_ptr<mi::Keymap> device_keymap;
};

MirKeyboardConfig::MirKeyboardConfig()
    : impl{std::make_unique<Implementation>()}
{}

MirKeyboardConfig::~MirKeyboardConfig() = default;

MirKeyboardConfig::MirKeyboardConfig(std::shared_ptr<mi::Keymap> keymap)
    : impl{std::make_unique<Implementation>(std::move(keymap))}
{
}

MirKeyboardConfig::MirKeyboardConfig(MirKeyboardConfig && other)
    : impl{std::move(other.impl)}
{
}

MirKeyboardConfig::MirKeyboardConfig(MirKeyboardConfig const& other)
    : impl{std::make_unique<Implementation>(*other.impl)}
{
}

MirKeyboardConfig& MirKeyboardConfig::operator=(MirKeyboardConfig const& other)
{
    *impl = *other.impl;
    return *this;
}

void MirKeyboardConfig::device_keymap(std::shared_ptr<mi::Keymap> keymap)
{
    impl->device_keymap = std::move(keymap);
}

auto MirKeyboardConfig::device_keymap() const -> std::shared_ptr<mir::input::Keymap> const&
{
    return impl->device_keymap;
}

bool MirKeyboardConfig::operator==(MirKeyboardConfig const& rhs) const
{
    return impl->device_keymap->matches(*rhs.device_keymap());
}

bool MirKeyboardConfig::operator!=(MirKeyboardConfig const& rhs) const
{
    return !(*this == rhs);
}

std::ostream& operator<<(std::ostream& out, MirKeyboardConfig const& keyboard)
{
    return out << keyboard.device_keymap()->model() << " config";
}
