/*
 * Copyright (C) 2018 Marius Gripsgard <marius@ubports.com>
 * Copyright (C) 2020 Canonical Ltd.
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
 */

#ifndef MIR_FRONTEND_XWAYLAND_WM_SHELLSURFACE_H
#define MIR_FRONTEND_XWAYLAND_WM_SHELLSURFACE_H

#include "wl_surface.h"
#include "xwayland_wm.h"
#include "xwayland_client_manager.h"
#include "xwayland_surface_role_surface.h"
#include "xwayland_surface_observer_surface.h"

#include <xcb/xcb.h>

#include <mutex>
#include <chrono>
#include <set>

namespace mir
{
namespace shell
{
class Shell;
class SurfaceSpecification;
}
namespace frontend
{
class WlSeat;
class XWaylandWM;
class XWaylandSurfaceObserver;

class XWaylandSurface
    : public XWaylandSurfaceRoleSurface,
      public XWaylandSurfaceObserverSurface
{
public:
    XWaylandSurface(
        XWaylandWM *wm,
        std::shared_ptr<XCBConnection> const& connection,
        XWaylandWMShell const& wm_shell,
        std::shared_ptr<XWaylandClientManager> const& client_manager,
        xcb_window_t window,
        geometry::Rectangle const& geometry,
        bool override_redirect,
        float scale);
    ~XWaylandSurface();

    void map();
    void close(); ///< Idempotent
    void take_focus();
    void configure_request(xcb_configure_request_event_t* event);
    void configure_notify(xcb_configure_notify_event_t* event);
    void net_wm_state_client_message(uint32_t const (&data)[5]);
    void wm_change_state_client_message(uint32_t const (&data)[5]);
    void property_notify(xcb_atom_t property);
    void attach_wl_surface(WlSurface* wl_surface); ///< Should only be called on the Wayland thread
    void move_resize(uint32_t detail);

private:
    // See https://specifications.freedesktop.org/wm-spec/wm-spec-1.3.html#idm45805407959456
    enum class NetWmStateAction: uint32_t
    {
        REMOVE = 0,
        ADD = 1,
        TOGGLE = 2,
    };

    /// contains more information than just a MirWindowState
    /// (for example if a minimized window would otherwise be maximized)
    /// TODO: delete this and use scene::SurfaceStateStack in it's place
    struct WindowState
    {
        bool withdrawn{true};
        bool minimized{false};
        bool maximized{false};
        bool fullscreen{false};

        void apply_change(
            std::shared_ptr<XCBConnection> const& connection,
            NetWmStateAction action,
            xcb_atom_t net_wm_state);

        auto operator==(WindowState const& that) const -> bool;
        auto mir_window_state() const -> MirWindowState;
        auto updated_from(MirWindowState state) const -> WindowState; ///< Does not change original
    };

    /// Overrides from XWaylandSurfaceObserverSurface
    /// @{
    void scene_surface_focus_set(bool has_focus) override;
    void scene_surface_state_set(MirWindowState new_state) override;
    void scene_surface_resized(geometry::Size const& new_size) override;
    void scene_surface_moved_to(geometry::Point const& new_top_left) override;
    void scene_surface_close_requested() override;
    /// @}

    /// Overrides from XWaylandSurfaceRoleSurface
    /// Should only be called on the Wayland thread
    /// @{
    void wl_surface_destroyed() override;
    auto scene_surface() const -> std::experimental::optional<std::shared_ptr<scene::Surface>> override;
    /// @}

    /// Creates a pending spec if needed and returns a reference
    auto pending_spec(
        std::lock_guard<std::mutex> const&) -> shell::SurfaceSpecification&;

    /// Clears the pending spec and returns what it was
    auto consume_pending_spec(
        std::lock_guard<std::mutex> const&) -> std::experimental::optional<std::unique_ptr<shell::SurfaceSpecification>>;

    /// Updates the pending spec
    void is_transient_for(xcb_window_t transient_for);

    /// Updates the window's WM_STATE and _NET_WM_STATE properties
    /// Should NOT be called under lock
    void inform_client_of_window_state(WindowState const& state);

    /// Requests the scene surface be put into the given state
    /// If the request results in an actual surface state change, the observer will be notified
    /// Should NOT be called under lock
    void request_scene_surface_state(MirWindowState new_state);

    auto latest_input_timestamp(std::lock_guard<std::mutex> const&) -> std::chrono::nanoseconds;

    /// Modifies the scene surface's geometry if needed, should not be called under lock
    void modify_surface_geometry(
        std::shared_ptr<scene::Surface> const& scene_surface,
        uint16_t xcb_value_mask,
        int16_t x, int16_t y,
        int16_t width, int16_t height);

    /// Appplies any mods in nullable_pending_spec to the scene_surface (if any)
    void apply_any_mods_to_scene_surface();

    /// Sets the position specified by the spec. top_left is always in global XWayland coordinates, even if parent is
    /// not null. If parent is given, it is set as the parent and an aux rect for relative placement is calculated. if
    /// parent is null, the location is specified in the global XWayland coordinates given. As always, you should call
    /// scale_surface_spec() before sending this spec to Mir.
    void surface_spec_set_position(
        shell::SurfaceSpecification& spec,
        scene::Surface* parent,
        geometry::Point top_left);

    /// One-stop-shop for scaling window modifications. Unlike with scaled Wayland surfaces, all the data going to and
    /// from the client is in raw pixels, but Mir internally deals with scaled coordinates. This means before punting
    /// our data off to Mir we need to scale it, which is what this function is for.
    void scale_surface_spec(shell::SurfaceSpecification& mods);

    /// Return data from any surface scaled to XWayland coordinates
    /// @{
    auto scaled_top_left_of(scene::Surface const& surface) -> geometry::Point;
    auto scaled_content_offset_of(scene::Surface const& surface) -> geometry::Displacement;
    auto scaled_content_size_of(scene::Surface const& surface) -> geometry::Size;
    /// @}

    /// Returns a surface that could act as this surface's parent, or nullptr if none
    auto plausible_parent(std::lock_guard<std::mutex> const&) -> std::shared_ptr<scene::Surface>;
    /// Applies cached.transient_for and cached.type to the spec
    void apply_cached_transient_for_and_type(std::lock_guard<std::mutex> const& lock);
    void wm_size_hints(std::vector<int32_t> const& hints);
    void motif_wm_hints(std::vector<uint32_t> const& hints);

    /// Returns the scene surface associated with a given xcb_window, or nullptr if none
    static auto xcb_window_get_scene_surface(XWaylandWM* xwm, xcb_window_t window) -> std::shared_ptr<scene::Surface>;

    XWaylandWM* const xwm;
    std::shared_ptr<XCBConnection> const connection;
    XWaylandWMShell const& wm_shell;
    std::shared_ptr<shell::Shell> const shell;
    std::shared_ptr<XWaylandClientManager> const client_manager;
    xcb_window_t const window;
    float const scale;
    std::map<xcb_window_t, std::function<std::function<void()>()>> const property_handlers;

    std::mutex mutable mutex;

    /// Cached version of properties on the X server
    struct
    {
        /// Reflects the _NET_WM_STATE and WM_STATE we have currently set on the window
        /// Should only be modified by set_wm_state()
        WindowState state;

        bool override_redirect;

        geometry::Size size;
        geometry::Point top_left; ///< Always in global coordinates

        /// The contents of the _NET_SUPPORTED property set by the client
        std::set<xcb_atom_t> supported_wm_protocols;

        /// True if server-side decorations have been explicitly disabled with motif hints
        bool motif_decorations_disabled{false};

        xcb_window_t transient_for{XCB_WINDOW_NONE};
        MirWindowType type{mir_window_type_freestyle};
    } cached;

    /// Set in set_wl_surface and cleared when a scene surface is created from it
    std::experimental::optional<std::shared_ptr<XWaylandSurfaceObserver>> surface_observer;
    std::unique_ptr<shell::SurfaceSpecification> nullable_pending_spec;
    std::shared_ptr<XWaylandClientManager::Session> client_session;
    std::weak_ptr<scene::Surface> weak_scene_surface;
    std::weak_ptr<scene::Surface> effective_parent;
};
} /* frontend */
} /* mir */

#endif // MIR_FRONTEND_XWAYLAND_WM_SHELLSURFACE_H
