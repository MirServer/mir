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

#ifndef MIR_INPUT_BUFFER_KEYMAP_H_
#define MIR_INPUT_BUFFER_KEYMAP_H_

#include "keymap.h"

#include <vector>
#include <string>
#include <memory>
#include <xkbcommon/xkbcommon.h>

namespace mir
{
namespace input
{

class BufferKeymap
    : public Keymap
{
public:
    BufferKeymap(std::string name, std::vector<char> buffer, xkb_keymap_format format);

    auto matches(Keymap const& other) const -> bool override;
    auto model() const -> std::string override;
    auto make_unique_xkb_keymap(xkb_context* context) const -> XKBKeymapPtr override;

private:
    std::string const name;
    std::vector<char> const buffer;
    xkb_keymap_format const format;
};

}
}

#endif // MIR_INPUT_BUFFER_KEYMAP_H_

