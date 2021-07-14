/*
 * Copyright © 2018 Canonical Ltd.
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
 * Authored by: Christopher James Halse Rogers <christopher.halse.rogers@canonical.com>
 */

#ifndef MIR_FRONTEND_WL_TOUCH_H
#define MIR_FRONTEND_WL_TOUCH_H

#include "wayland_wrapper.h"

#include "mir/geometry/point.h"

#include <unordered_map>
#include <functional>
#include <chrono>

struct MirTouchEvent;

namespace mir
{
class Executor;
namespace time
{
class Clock;
}

namespace frontend
{
class WlSurface;

class WlTouch : public wayland::Touch
{
public:
    WlTouch(wl_resource* new_resource, std::shared_ptr<time::Clock> const& clock);

    ~WlTouch();

    /// Convert the Mir event into Wayland events and send them to the client. root_surface is the one that received
    /// the Mir event, but the final Wayland event may be sent to a subsurface.
    void event(MirTouchEvent const* event, WlSurface& root_surface);

private:
    struct TouchedSurface
    {
        wayland::Weak<WlSurface> surface;
        wayland::DestroyListenerId destroy_listener_id;
    };

    std::shared_ptr<time::Clock> const clock;
    /// Maps touch IDs to the surfaces the touch is on
    std::unordered_map<int32_t, TouchedSurface> touch_id_to_surface;
    bool needs_frame{false};

    void down(
        uint32_t serial,
        std::chrono::milliseconds const& ms,
        int32_t touch_id,
        WlSurface& root_surface,
        std::pair<float, float> const& root_position);
    void motion(
        std::chrono::milliseconds const& ms,
        int32_t touch_id,
        std::pair<float, float> const& root_position);
    void up(uint32_t serial, std::chrono::milliseconds const& ms, int32_t touch_id);
    void maybe_frame();
};

}
}

#endif // MIR_FRONTEND_WL_TOUCH_H
