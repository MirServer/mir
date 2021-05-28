/*
 * Copyright © 2018-2019 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
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
 * Authored by: William Wold <william.wold@canonical.com>
 */

#include "virtual_keyboard_v1.h"

#include "virtual-keyboard-unstable-v1_wrapper.h"
#include "wl_seat.h"

#include "mir/input/seat.h"
#include "mir/fatal.h"
#include "mir/log.h"

namespace mf = mir::frontend;
namespace mw = mir::wayland;
namespace mi = mir::input;

namespace mir
{
namespace frontend
{
class VirtualKeyboardManagerV1Global
    : public wayland::VirtualKeyboardManagerV1::Global
{
public:
    VirtualKeyboardManagerV1Global(wl_display* display);

private:
    void bind(wl_resource* new_resource) override;
};

class VirtualKeyboardManagerV1
    : public wayland::VirtualKeyboardManagerV1
{
public:
    VirtualKeyboardManagerV1(wl_resource* resource, VirtualKeyboardManagerV1Global const& global);

private:
    void create_virtual_keyboard(struct wl_resource* seat, struct wl_resource* id) override;
};

class VirtualKeyboardV1
    : public wayland::VirtualKeyboardV1
{
public:
    VirtualKeyboardV1(wl_resource* resource, WlSeat& seat);

private:
    void keymap(uint32_t format, mir::Fd fd, uint32_t size) override;
    void key(uint32_t time, uint32_t key, uint32_t state) override;
    void modifiers(uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) override;
};
}
}

auto mf::create_virtual_keyboard_manager_v1(wl_display* display) -> std::shared_ptr<VirtualKeyboardManagerV1Global>
{
    return std::make_shared<VirtualKeyboardManagerV1Global>(display);
}

mf::VirtualKeyboardManagerV1Global::VirtualKeyboardManagerV1Global(wl_display* display)
    : Global{display, Version<1>()}
{
}

void mf::VirtualKeyboardManagerV1Global::bind(wl_resource* new_resource)
{
    new VirtualKeyboardManagerV1{new_resource, *this};
}

mf::VirtualKeyboardManagerV1::VirtualKeyboardManagerV1(
    wl_resource* resource,
    VirtualKeyboardManagerV1Global const& global)
    : wayland::VirtualKeyboardManagerV1{resource, Version<1>()}
{
    (void)global;
}

void mf::VirtualKeyboardManagerV1::create_virtual_keyboard(struct wl_resource* seat, struct wl_resource* id)
{
    auto const wl_seat = WlSeat::from(seat);
    if (!wl_seat)
    {
        fatal_error("create_virtual_keyboard() received invalid wl_seat");
    }
    new VirtualKeyboardV1{id, *wl_seat};
}

mf::VirtualKeyboardV1::VirtualKeyboardV1(wl_resource* resource, WlSeat& seat)
    : wayland::VirtualKeyboardV1{resource, Version<1>()}
{
    (void)seat;
}

void mf::VirtualKeyboardV1::keymap(uint32_t format, mir::Fd fd, uint32_t size)
{
    (void)format;
    (void)fd;
    (void)size;
    // TODO
    log_info("Ignoring zwp_virtual_keyboard_v1.keymap()");
}

void mf::VirtualKeyboardV1::key(uint32_t time, uint32_t key, uint32_t state)
{
    (void)time;
    (void)key;
    (void)state;
    // TODO
    log_info("Ignoring zwp_virtual_keyboard_v1.key()");
}

void mf::VirtualKeyboardV1::modifiers(
    uint32_t mods_depressed,
    uint32_t mods_latched,
    uint32_t mods_locked,
    uint32_t group)
{
    (void)mods_depressed;
    (void)mods_latched;
    (void)mods_locked;
    (void)group;
    // TODO
    log_info("Ignoring zwp_virtual_keyboard_v1.modifiers()");
}
