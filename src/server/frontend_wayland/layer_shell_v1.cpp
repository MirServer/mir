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
 * Authored by: William Wold <william.wold@canonical.com>
 */

#include "layer_shell_v1.h"

#include "wl_surface.h"
#include "window_wl_surface_role.h"
#include "xdg_shell_stable.h"
#include "wayland_utils.h"
#include "output_manager.h"

#include "mir/shell/surface_specification.h"
#include "mir/log.h"
#include <boost/throw_exception.hpp>
#include <deque>
#include <vector>
#include <algorithm>

namespace mf = mir::frontend;
namespace ms = mir::scene;
namespace msh = mir::shell;
namespace geom = mir::geometry;
namespace mw = mir::wayland;

namespace
{

auto layer_shell_layer_to_mir_depth_layer(uint32_t layer) -> MirDepthLayer
{
    switch (layer)
    {
    case mw::LayerShellV1::Layer::background:
        return mir_depth_layer_background;
    case mw::LayerShellV1::Layer::bottom:
        return mir_depth_layer_below;
    case mw::LayerShellV1::Layer::top:
        return mir_depth_layer_above;
    case mw::LayerShellV1::Layer::overlay:
        return mir_depth_layer_overlay;
    default:
        BOOST_THROW_EXCEPTION(std::runtime_error("Invalid Layer Shell layer " + std::to_string(layer)));
    }
}

}

namespace mir
{
namespace frontend
{

class LayerShellV1::Instance : wayland::LayerShellV1
{
public:
    Instance(wl_resource* new_resource, mf::LayerShellV1* shell)
        : LayerShellV1{new_resource, Version<3>()},
          shell{shell}
    {
    }

private:
    void get_layer_surface(
        wl_resource* new_layer_surface,
        wl_resource* surface,
        std::experimental::optional<wl_resource*> const& output,
        uint32_t layer,
        std::string const& namespace_) override;
    void destroy() override;

    mf::LayerShellV1* const shell;
};

class LayerSurfaceV1 : public wayland::LayerSurfaceV1, public WindowWlSurfaceRole
{
public:
    LayerSurfaceV1(
        wl_resource* new_resource,
        WlSurface* surface,
        std::experimental::optional<graphics::DisplayConfigurationOutputId> output_id,
        LayerShellV1 const& layer_shell,
        MirDepthLayer layer);

    ~LayerSurfaceV1() = default;

    static auto from(wl_resource* surface) -> std::experimental::optional<LayerSurfaceV1*>;

private:
    template<typename T>
    class DoubleBuffered
    {
    public:
        DoubleBuffered() = default;
        DoubleBuffered(T const& initial) : _committed{initial} {}
        DoubleBuffered(DoubleBuffered const&) = delete;
        auto operator=(DoubleBuffered const&) const -> bool = delete;

        auto pending() const -> T const& { return _pending ? _pending.value() : _committed; }
        auto is_pending() const -> bool { return _pending; }
        void set_pending(T const& value) { _pending = value; }
        auto committed() const -> T const& { return _committed; }
        void commit()
        {
            if (_pending)
            {
                _committed = _pending.value();
            }
            _pending = std::experimental::nullopt;
        }

    private:
        std::experimental::optional<T> _pending;
        T _committed;
    };

    struct OptionalSize
    {
        std::optional<geom::Width> width;
        std::optional<geom::Height> height;
    };

    struct Margin
    {
        geom::DeltaX left{0};
        geom::DeltaX right{0};
        geom::DeltaY top{0};
        geom::DeltaY bottom{0};
    };

    struct Anchors
    {
        bool left{false};
        bool right{false};
        bool top{false};
        bool bottom{false};
    };

    /// Returns all anchored edges ored together
    auto get_placement_gravity() const -> MirPlacementGravity;

    /// Returns the edge from which we can have an exclusive zone
    /// Returns only left, right, top, bottom, or center if we are not anchored to an edge
    auto get_anchored_edge() const -> MirPlacementGravity;

    /// Returns the rectangele in surface coords that this surface is exclusive for (if any)
    auto get_exclusive_rect() const -> std::experimental::optional<geom::Rectangle>;

    static auto horiz_padding(Anchors const& anchors, Margin const& margin) -> geom::DeltaX;
    static auto vert_padding(Anchors const& anchors, Margin const& margin) -> geom::DeltaY;

    /// Returns the size requested from the window manager minus the margin (which the raw requested size includes)
    auto unpadded_requested_size() const -> geom::Size;

    /// Sets pending state on the WindowWlSurfaceRole
    auto inform_window_role_of_pending_placement();

    /// Sends a configure event if needed
    void configure();

    // from wayland::LayerSurfaceV1
    void set_size(uint32_t width, uint32_t height) override;
    void set_anchor(uint32_t anchor) override;
    void set_exclusive_zone(int32_t zone) override;
    void set_margin(int32_t top, int32_t right, int32_t bottom, int32_t left) override;
    void set_keyboard_interactivity(uint32_t keyboard_interactivity) override;
    void get_popup(wl_resource* popup) override;
    void ack_configure(uint32_t serial) override;
    void destroy() override;
    void set_layer(uint32_t layer) override;

    // from WindowWlSurfaceRole
    void handle_commit() override;
    void handle_state_change(MirWindowState /*new_state*/) override {};
    void handle_active_change(bool /*is_now_active*/) override {};
    void handle_resize(
        std::experimental::optional<geometry::Point> const& /*new_top_left*/,
        geometry::Size const& /*new_size*/) override;
    void handle_close_request() override;

    DoubleBuffered<uint32_t> exclusive_zone{0};
    DoubleBuffered<Anchors> anchors;
    DoubleBuffered<Margin> margin;
    DoubleBuffered<OptionalSize> opt_size;
    DoubleBuffered<geometry::Displacement> offset;
    bool configure_on_next_commit{true}; ///< If to send a .configure event at the end of the next or current commit
    std::deque<std::pair<uint32_t, OptionalSize>> inflight_configures;
    std::vector<wayland::Weak<XdgPopupStable>> popups; ///< We have to keep track of popups to adjust their offset

    std::optional<geometry::Width> width_requested = std::nullopt;
    std::optional<geometry::Height> height_requested = std::nullopt;

    void send_configure(OptionalSize configure_size);
};

}
}

// LayerShellV1

mf::LayerShellV1::LayerShellV1(
    struct wl_display* display,
    Executor& wayland_executor,
    std::shared_ptr<msh::Shell> shell,
    WlSeat& seat,
    OutputManager* output_manager)
    : Global(display, Version<3>()),
      wayland_executor{wayland_executor},
      shell{shell},
      seat{seat},
      output_manager{output_manager}
{
}

auto mf::LayerShellV1::get_window(wl_resource* surface) -> std::shared_ptr<ms::Surface>
{
    namespace mw = mir::wayland;

    if (mw::LayerSurfaceV1::is_instance(surface))
    {
        if (auto const layer_surface = LayerSurfaceV1::from(surface))
        {
            if (auto const scene_surface = layer_surface.value()->scene_surface())
            {
                return scene_surface.value();
            }
        }

        log_debug("No window currently associated with wayland::LayerSurfaceV1 %p", static_cast<void*>(surface));
    }

    return {};
}

void mf::LayerShellV1::bind(wl_resource* new_resource)
{
    new Instance{new_resource, this};
}

void mf::LayerShellV1::Instance::get_layer_surface(
    wl_resource* new_layer_surface,
    wl_resource* surface,
    std::experimental::optional<wl_resource*> const& output,
    uint32_t layer,
    std::string const& namespace_)
{
    (void)namespace_; // Can be ignored if no special behavior is required

    auto const output_id = output ?
        shell->output_manager->output_id_for(client, output.value()) :
        std::experimental::nullopt;

    new LayerSurfaceV1(
        new_layer_surface,
        WlSurface::from(surface),
        output_id,
        *shell,
        layer_shell_layer_to_mir_depth_layer(layer));
}

void mf::LayerShellV1::Instance::destroy()
{
    destroy_wayland_object();
}

// LayerSurfaceV1

mf::LayerSurfaceV1::LayerSurfaceV1(
    wl_resource* new_resource,
    WlSurface* surface,
    std::experimental::optional<graphics::DisplayConfigurationOutputId> output_id,
    LayerShellV1 const& layer_shell,
    MirDepthLayer layer)
    : mw::LayerSurfaceV1(new_resource, Version<3>()),
      WindowWlSurfaceRole(
          layer_shell.wayland_executor,
          &layer_shell.seat,
          wayland::LayerSurfaceV1::client,
          surface,
          layer_shell.shell,
          layer_shell.output_manager)
{
    // TODO: Error if surface has buffer attached or committed
    shell::SurfaceSpecification spec;
    spec.state = mir_window_state_attached;
    spec.depth_layer = layer;
    if (output_id)
        spec.output_id = output_id.value();
    apply_spec(spec);
}

auto mf::LayerSurfaceV1::from(wl_resource* surface) -> std::experimental::optional<LayerSurfaceV1*>
{
    if (!mw::LayerSurfaceV1::is_instance(surface))
        return std::experimental::nullopt;
    auto const mw_surface = mw::LayerSurfaceV1::from(surface);
    auto const mf_surface = dynamic_cast<mf::LayerSurfaceV1*>(mw_surface);
    if (mf_surface)
        return mf_surface;
    else
        return std::experimental::nullopt;
}

auto mf::LayerSurfaceV1::get_placement_gravity() const -> MirPlacementGravity
{
    MirPlacementGravity edges = mir_placement_gravity_center;

    if (anchors.pending().left)
        edges = MirPlacementGravity(edges | mir_placement_gravity_west);

    if (anchors.pending().right)
        edges = MirPlacementGravity(edges | mir_placement_gravity_east);

    if (anchors.pending().top)
        edges = MirPlacementGravity(edges | mir_placement_gravity_north);

    if (anchors.pending().bottom)
        edges = MirPlacementGravity(edges | mir_placement_gravity_south);

    return edges;
}

auto mf::LayerSurfaceV1::get_anchored_edge() const -> MirPlacementGravity
{
    MirPlacementGravity h_edge = mir_placement_gravity_center;
    MirPlacementGravity v_edge = mir_placement_gravity_center;

    if (anchors.pending().left != anchors.pending().right)
    {
        if (anchors.pending().left)
            h_edge = mir_placement_gravity_west;
        else
            h_edge = mir_placement_gravity_east;
    }

    if (anchors.pending().top != anchors.pending().bottom)
    {
        if (anchors.pending().top)
            v_edge = mir_placement_gravity_north;
        else
            v_edge = mir_placement_gravity_south;
    }

    if (h_edge == mir_placement_gravity_center)
        return v_edge;
    else if (v_edge == mir_placement_gravity_center)
        return h_edge;
    else
        return mir_placement_gravity_center;
}

auto mf::LayerSurfaceV1::get_exclusive_rect() const -> std::experimental::optional<geom::Rectangle>
{
    auto zone = exclusive_zone.pending();
    if (zone <= 0)
        return std::experimental::nullopt;

    auto edge = get_anchored_edge();
    auto size = pending_size(); // includes margin padding

    switch (edge)
    {
    case mir_placement_gravity_west:
        return geom::Rectangle{
            {0, 0},
            {geom::Width{zone} + margin.pending().left, size.height}};

    case mir_placement_gravity_east:
        return geom::Rectangle{
            {geom::as_x(size.width) - geom::DeltaX{zone} - margin.pending().right, 0},
            {geom::Width{zone} + margin.pending().right, size.height}};

    case mir_placement_gravity_north:
        return geom::Rectangle{
            {0, 0},
            {size.width, geom::Height{zone} + margin.pending().top}};

    case mir_placement_gravity_south:
        return geom::Rectangle{
            {0, geom::as_y(size.height) - geom::DeltaY{zone} - margin.pending().bottom},
            {size.width, geom::Height{zone} + margin.pending().bottom}};

    default:
        return std::experimental::nullopt;
    }
}

auto mf::LayerSurfaceV1::horiz_padding(Anchors const& anchors, Margin const& margin) -> geom::DeltaX
{
    return (anchors.left ? margin.left : geom::DeltaX{}) +
        (anchors.right ? margin.right : geom::DeltaX{});
}

auto mf::LayerSurfaceV1::vert_padding(Anchors const& anchors, Margin const& margin) -> geom::DeltaY
{
    return (anchors.top ? margin.top : geom::DeltaY{}) +
        (anchors.bottom ? margin.bottom : geom::DeltaY{});
}

auto mf::LayerSurfaceV1::unpadded_requested_size() const -> geom::Size
{
    auto size = requested_window_size().value_or(current_size());
    size.width -= horiz_padding(anchors.committed(), margin.committed());
    size.height -= vert_padding(anchors.committed(), margin.committed());
    return size;
}

auto mf::LayerSurfaceV1::inform_window_role_of_pending_placement()
{
    if (opt_size.pending().width)
    {
        auto width = opt_size.pending().width.value();
        width += horiz_padding(anchors.pending(), margin.pending());
        set_pending_width(width);
    }

    if (opt_size.pending().height)
    {
        auto height = opt_size.pending().height.value();
        height += vert_padding(anchors.pending(), margin.pending());
        set_pending_height(height);
    }

    offset.set_pending({
        anchors.pending().left ? margin.pending().left : geom::DeltaX{},
        anchors.pending().top ? margin.pending().top : geom::DeltaY{}});

    set_pending_offset(offset.pending());

    shell::SurfaceSpecification spec;

    spec.attached_edges = get_placement_gravity();

    auto const exclusive_rect = get_exclusive_rect();
    spec.exclusive_rect = exclusive_rect ?
        mir::optional_value<geom::Rectangle>(exclusive_rect.value()) :
        mir::optional_value<geom::Rectangle>();

    apply_spec(spec);
}

void mf::LayerSurfaceV1::configure()
{
    // TODO: Error if told to configure on an axis the window is not streatched along

    OptionalSize configure_size{std::nullopt, std::nullopt};
    auto requested = unpadded_requested_size();

    if (anchors.committed().left && anchors.committed().right)
    {
        configure_size.width = requested.width;
    }

    if (anchors.committed().bottom && anchors.committed().top)
    {
        configure_size.height = requested.height;
    }

    if (opt_size.committed().width)
    {
        configure_size.width = opt_size.committed().width.value();
    }

    if (opt_size.committed().height)
    {
        configure_size.height = opt_size.committed().height.value();
    }

    send_configure(configure_size);
}

void mf::LayerSurfaceV1::send_configure(OptionalSize configure_size)
{
    auto const serial = wl_display_next_serial(wl_client_get_display(wayland::LayerSurfaceV1::client));
    if (!inflight_configures.empty() && serial <= inflight_configures.back().first)
        BOOST_THROW_EXCEPTION(std::runtime_error("Generated invalid configure serial"));
    inflight_configures.push_back(std::make_pair(serial, configure_size));

    send_configure_event(
        serial,
        configure_size.width.value_or(geom::Width{0}).as_uint32_t(),
        configure_size.height.value_or(geom::Height{0}).as_uint32_t());
}

void mf::LayerSurfaceV1::set_size(uint32_t width, uint32_t height)
{
    width_requested = (width  > 0) ? std::optional<geometry::Width>{geom::Width{width}} : std::nullopt;
    height_requested = (height > 0) ? std::optional<geometry::Height>{geom::Height{height}} : std::nullopt;

    opt_size.set_pending(OptionalSize{width_requested, height_requested});
    inform_window_role_of_pending_placement();
    configure_on_next_commit = true;
}

void mf::LayerSurfaceV1::set_anchor(uint32_t anchor)
{
    anchors.set_pending(Anchors{
        static_cast<bool>(anchor & Anchor::left),
        static_cast<bool>(anchor & Anchor::right),
        static_cast<bool>(anchor & Anchor::top),
        static_cast<bool>(anchor & Anchor::bottom)});
    inform_window_role_of_pending_placement();
}

void mf::LayerSurfaceV1::set_exclusive_zone(int32_t zone)
{
    exclusive_zone.set_pending(zone);
    inform_window_role_of_pending_placement();
}

void mf::LayerSurfaceV1::set_margin(int32_t top, int32_t right, int32_t bottom, int32_t left)
{
    margin.set_pending(Margin{
        geom::DeltaX{left},
        geom::DeltaX{right},
        geom::DeltaY{top},
        geom::DeltaY{bottom}});
    inform_window_role_of_pending_placement();
}

void mf::LayerSurfaceV1::set_keyboard_interactivity(uint32_t keyboard_interactivity)
{
    (void)keyboard_interactivity;
}

void mf::LayerSurfaceV1::get_popup(struct wl_resource* popup)
{
    auto const scene_surface_ = scene_surface();
    if (!scene_surface_)
    {
        log_warning("Layer surface can not be a popup parent because it does not have a Mir surface");
        return;
    }

    auto* const popup_window_role = XdgPopupStable::from(popup);

    popup_window_role->set_aux_rect_offset_now(offset.pending());

    mir::shell::SurfaceSpecification spec;
    spec.parent = scene_surface_.value();
    popup_window_role->apply_spec(spec);

    // Ideally we'd do this in a callback when popups are destroyed, but in practice waiting until a new popup is
    // created to clear out the destroyed ones is fine
    popups.erase(
        std::remove_if(
            popups.begin(),
            popups.end(),
            [](auto popup) { return !popup; }),
        popups.end());
    popups.push_back(mw::make_weak(popup_window_role));
}

void mf::LayerSurfaceV1::ack_configure(uint32_t serial)
{
    while (!inflight_configures.empty() && inflight_configures.front().first < serial)
    {
        inflight_configures.pop_front();
    }

    if (inflight_configures.empty() || inflight_configures.front().first != serial)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error(
            "Could not find acked configure with serial " + std::to_string(serial)));
    }

    opt_size.set_pending(inflight_configures.front().second);
    inflight_configures.pop_front();

    // Do NOT set configure_on_next_commit (as we do in the other case when opt_size is set)
    // We don't want to make the client acking one configure result in us sending another
}

void mf::LayerSurfaceV1::destroy()
{
    destroy_wayland_object();
}

void mf::LayerSurfaceV1::set_layer(uint32_t layer)
{
    shell::SurfaceSpecification spec;
    spec.depth_layer = layer_shell_layer_to_mir_depth_layer(layer);
    apply_spec(spec);
    // Don't use inform_window_role_of_pending_placement() because the layer doesn't interfere with any other properties
}

void mf::LayerSurfaceV1::handle_commit()
{
    exclusive_zone.commit();
    anchors.commit();
    margin.commit();
    opt_size.commit();

    if (offset.pending() != offset.committed())
    {
        // When offset changes, every popup's aux rect needs to be shifted
        for (auto const& popup : popups)
        {
            if (popup)
            {
                popup.value().set_aux_rect_offset_now(offset.pending());
            }
        }
    }
    offset.commit();

    // wlr-layer-shell-unstable-v1.xml:
    // "You must set your anchor to opposite edges in the dimensions you omit; not doing so is a protocol error."
    bool const horiz_stretched = anchors.committed().left && anchors.committed().right;
    bool const vert_stretched = anchors.committed().top && anchors.committed().bottom;
    if (!horiz_stretched && !opt_size.committed().width)
    {
        BOOST_THROW_EXCEPTION(mw::ProtocolError(
            resource,
            Error::invalid_size,
            "Width may be unspecified only when surface is anchored to left and right edges"));
    }
    if (!vert_stretched && !opt_size.committed().height)
    {
        BOOST_THROW_EXCEPTION(mw::ProtocolError(
            resource,
            Error::invalid_size,
            "Height may be unspecified only when surface is anchored to top and bottom edges"));
    }

    if (configure_on_next_commit)
    {
        configure();
        configure_on_next_commit = false;
    }
}

void mf::LayerSurfaceV1::handle_resize(
    std::experimental::optional<geom::Point> const& /*new_top_left*/,
    geom::Size const& new_size)
{
    send_configure({
        width_requested.value_or(
            new_size.width - horiz_padding(anchors.committed(), margin.committed())),
        height_requested.value_or(
            new_size.height - vert_padding(anchors.committed(), margin.committed()))});
}

void mf::LayerSurfaceV1::handle_close_request()
{
    send_closed_event();
}
