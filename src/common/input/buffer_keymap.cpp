/*
 * Copyright © 2021 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored By: William Wold <william.wold@canoncical.com>
 */

#include "mir/input/buffer_keymap.h"

#include <stdexcept>
#include <boost/throw_exception.hpp>
#include <cstring>

namespace mi = mir::input;

mi::BufferKeymap::BufferKeymap(std::string name, std::vector<char> buffer, xkb_keymap_format format)
    : name{name},
      buffer{std::move(buffer)},
      format{format}
{
    static_assert(sizeof(buffer[0]) == 1, "implemenation assumes buffer.size() is in bytes");
}

auto mi::BufferKeymap::matches(Keymap const& other) const -> bool
{
    auto const keymap = dynamic_cast<BufferKeymap const*>(&other);
    return keymap &&
        buffer.size() == keymap->buffer.size() &&
        memcmp(buffer.data(), keymap->buffer.data(), buffer.size()) == 0;
}

auto mi::BufferKeymap::model() const -> std::string
{
    return name;
}

auto mi::BufferKeymap::make_unique_xkb_keymap(xkb_context* context) const -> XKBKeymapPtr
{
    auto keymap_ptr = xkb_keymap_new_from_buffer(
        context,
        buffer.data(),
        buffer.size(),
        format,
        xkb_keymap_compile_flags(0));

    if (!keymap_ptr)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("failed to build keymap from buffer for " + name));
    }
    return {keymap_ptr, &xkb_keymap_unref};
}
