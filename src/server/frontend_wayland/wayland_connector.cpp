/*
 * Copyright © 2015-2019 Canonical Ltd.
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

#include "wayland_connector.h"

#include "wl_client.h"
#include "wl_data_device_manager.h"
#include "wayland_utils.h"
#include "wl_surface_role.h"
#include "window_wl_surface_role.h"
#include "wl_subcompositor.h"
#include "wl_surface.h"
#include "wl_seat.h"
#include "wl_region.h"
#include "frame_executor.h"

#include "output_manager.h"
#include "wayland_executor.h"

#include "wayland_wrapper.h"

#include "mir/frontend/wayland.h"

#include "mir/main_loop.h"

#include "mir/compositor/buffer_stream.h"

#include "mir/scene/surface_creation_parameters.h"
#include "mir/shell/shell.h"
#include "mir/scene/surface.h"
#include <mir/thread_name.h>

#include "mir/graphics/buffer_properties.h"
#include "mir/graphics/buffer.h"
#include "mir/graphics/graphic_buffer_allocator.h"

#include "mir/renderer/gl/texture_target.h"
#include "mir/frontend/buffer_stream_id.h"

#include "mir/input/seat.h"
#include "mir/input/device.h"
#include "mir/input/mir_keyboard_config.h"
#include "mir/input/input_device_hub.h"
#include "mir/input/input_device_observer.h"

#include <system_error>
#include <sys/eventfd.h>
#include <wayland-server-core.h>
#include <unordered_map>
#include <boost/throw_exception.hpp>

#include <functional>
#include <type_traits>

#include <xkbcommon/xkbcommon.h>
#include <linux/input.h>
#include <mir/log.h>
#include <cstring>

#include "mir/fd.h"

#include <sys/stat.h>
#include <sys/socket.h>
#include <unordered_set>
#include "mir/anonymous_shm_file.h"

namespace mf = mir::frontend;
namespace mg = mir::graphics;
namespace mc = mir::compositor;
namespace ms = mir::scene;
namespace msh = mir::shell;
namespace msh = mir::shell;
namespace geom = mir::geometry;
namespace mi = mir::input;
namespace mw = mir::wayland;

namespace mir
{
namespace frontend
{
class WlCompositor : public wayland::Compositor::Global
{
public:
    WlCompositor(
        struct wl_display* display,
        std::shared_ptr<mir::Executor> const& wayland_executor,
        std::shared_ptr<mir::Executor> const& frame_callback_executor,
        std::shared_ptr<mg::GraphicBufferAllocator> const& allocator)
        : Global(display, Version<4>()),
          allocator{allocator},
          wayland_executor{wayland_executor},
          frame_callback_executor{frame_callback_executor}
    {
    }

    void on_surface_created(wl_client* client, uint32_t id, std::function<void(WlSurface*)> const& callback);

private:
    std::shared_ptr<mg::GraphicBufferAllocator> const allocator;
    std::shared_ptr<mir::Executor> const wayland_executor;
    std::shared_ptr<mir::Executor> const frame_callback_executor;
    std::map<std::pair<wl_client*, uint32_t>, std::vector<std::function<void(WlSurface*)>>> surface_callbacks;

    class Instance : wayland::Compositor
    {
    public:
        Instance(wl_resource* new_resource, WlCompositor* compositor)
            : mw::Compositor{new_resource, Version<4>()},
              compositor{compositor}
        {
        }

    private:
        void create_surface(wl_resource* new_surface) override;
        void create_region(wl_resource* new_region) override;
        WlCompositor* const compositor;
    };

    void bind(wl_resource* new_resource)
    {
        new Instance{new_resource, this};
    }
};

void WlCompositor::on_surface_created(wl_client* client, uint32_t id, std::function<void(WlSurface*)> const& callback)
{
    wl_resource* resource = wl_client_get_object(client, id);
    auto const wl_surface = resource ? WlSurface::from(resource) : nullptr;
    if (wl_surface)
    {
        callback(wl_surface);
    }
    else
    {
        surface_callbacks[std::make_pair(client, id)].push_back(callback);
    }
}

void WlCompositor::Instance::create_surface(wl_resource* new_surface)
{
    auto const surface = new WlSurface{
        new_surface,
        compositor->wayland_executor,
        compositor->frame_callback_executor,
        compositor->allocator};
    auto const key = std::make_pair(wl_resource_get_client(new_surface), wl_resource_get_id(new_surface));
    auto const callbacks = compositor->surface_callbacks.find(key);
    if (callbacks != compositor->surface_callbacks.end())
    {
        for (auto const& callback : callbacks->second)
        {
            callback(surface);
        }
        compositor->surface_callbacks.erase(callbacks);
    }
    // Wayland ojects delete themselves
}

void WlCompositor::Instance::create_region(wl_resource* new_region)
{
    new WlRegion{new_region};
}

class WlShellSurface : public wayland::ShellSurface, public WindowWlSurfaceRole
{
public:
    WlShellSurface(
        wl_resource* new_resource,
        Executor& wayland_executor,
        WlSurface* surface,
        std::shared_ptr<msh::Shell> const& shell,
        WlSeat& seat,
        OutputManager* output_manager)
        : ShellSurface(new_resource, Version<1>()),
          WindowWlSurfaceRole{wayland_executor, &seat, wayland::ShellSurface::client, surface, shell, output_manager}
    {
    }

    ~WlShellSurface() = default;

protected:
    void surface_destroyed() override
    {
        // The spec is a little contradictory:
        // wl_surface: When a client wants to destroy a wl_surface, they must destroy this 'role object' before the wl_surface
        // wl_shell_surface: On the server side the object is automatically destroyed when the related wl_surface is destroyed
        // Without a destroy request, it seems the latter must be correct, so that is what we implement
        destroy_and_delete();
    }

    void set_toplevel() override
    {
    }

    void set_transient(
        struct wl_resource* parent,
        int32_t x,
        int32_t y,
        uint32_t flags) override
    {
        auto& parent_surface = *WlSurface::from(parent);

        mir::shell::SurfaceSpecification mods;

        if (flags & WL_SHELL_SURFACE_TRANSIENT_INACTIVE)
            mods.type = mir_window_type_gloss;
        if (auto parent = parent_surface.scene_surface())
            mods.parent = parent.value();
        mods.aux_rect = geom::Rectangle{{x, y}, {}};
        mods.surface_placement_gravity = mir_placement_gravity_northwest;
        mods.aux_rect_placement_gravity = mir_placement_gravity_southeast;
        mods.placement_hints = mir_placement_hints_slide_x;
        mods.aux_rect_placement_offset_x = 0;
        mods.aux_rect_placement_offset_y = 0;

        apply_spec(mods);
    }

    void handle_commit() override {};
    void handle_state_change(MirWindowState /*new_state*/) override {};
    void handle_active_change(bool /*is_now_active*/) override {};

    void handle_resize(std::experimental::optional<geometry::Point> const& /*new_top_left*/,
                       geometry::Size const& new_size) override
    {
        wl_shell_surface_send_configure(resource, WL_SHELL_SURFACE_RESIZE_NONE, new_size.width.as_int(),
                                        new_size.height.as_int());
    }

    void handle_close_request() override
    {
        // It seems there is no way to request close of a wl_shell_surface
    }

    void set_fullscreen(
        uint32_t /*method*/,
        uint32_t /*framerate*/,
        std::experimental::optional<struct wl_resource*> const& output) override
    {
        WindowWlSurfaceRole::set_fullscreen(output);
    }

    void set_popup(
        struct wl_resource* /*seat*/,
        uint32_t /*serial*/,
        struct wl_resource* parent,
        int32_t x,
        int32_t y,
        uint32_t flags) override
    {
        auto& parent_surface = *WlSurface::from(parent);

        mir::shell::SurfaceSpecification mods;

        if (flags & WL_SHELL_SURFACE_TRANSIENT_INACTIVE)
            mods.type = mir_window_type_gloss;
        if (auto parent = parent_surface.scene_surface())
            mods.parent = parent.value();
        mods.aux_rect = geom::Rectangle{{x, y}, {}};
        mods.surface_placement_gravity = mir_placement_gravity_northwest;
        mods.aux_rect_placement_gravity = mir_placement_gravity_southeast;
        mods.placement_hints = mir_placement_hints_slide_x;
        mods.aux_rect_placement_offset_x = 0;
        mods.aux_rect_placement_offset_y = 0;

        apply_spec(mods);
    }

    void set_maximized(std::experimental::optional<struct wl_resource*> const& output) override
    {
        (void)output;
        WindowWlSurfaceRole::add_state_now(mir_window_state_maximized);
    }

    void set_title(std::string const& title) override
    {
        WindowWlSurfaceRole::set_title(title);
    }

    void pong(uint32_t /*serial*/) override
    {
    }

    void move(struct wl_resource* /*seat*/, uint32_t /*serial*/) override
    {
        WindowWlSurfaceRole::initiate_interactive_move();
    }

    void resize(struct wl_resource* /*seat*/, uint32_t /*serial*/, uint32_t edges) override
    {
        MirResizeEdge edge = mir_resize_edge_none;

        switch (edges)
        {
        case WL_SHELL_SURFACE_RESIZE_TOP:
            edge = mir_resize_edge_north;
            break;

        case WL_SHELL_SURFACE_RESIZE_BOTTOM:
            edge = mir_resize_edge_south;
            break;

        case WL_SHELL_SURFACE_RESIZE_LEFT:
            edge = mir_resize_edge_west;
            break;

        case WL_SHELL_SURFACE_RESIZE_TOP_LEFT:
            edge = mir_resize_edge_northwest;
            break;

        case WL_SHELL_SURFACE_RESIZE_BOTTOM_LEFT:
            edge = mir_resize_edge_southwest;
            break;

        case WL_SHELL_SURFACE_RESIZE_RIGHT:
            edge = mir_resize_edge_east;
            break;

        case WL_SHELL_SURFACE_RESIZE_TOP_RIGHT:
            edge = mir_resize_edge_northeast;
            break;

        case WL_SHELL_SURFACE_RESIZE_BOTTOM_RIGHT:
            edge = mir_resize_edge_southeast;
            break;

        default:;
        }

        WindowWlSurfaceRole::initiate_interactive_resize(edge);
    }

    void set_class(std::string const& /*class_*/) override
    {
    }
};

class WlShell : public wayland::Shell::Global
{
public:
    WlShell(
        wl_display* display,
        Executor& wayland_executor,
        std::shared_ptr<msh::Shell> const& shell,
        WlSeat& seat,
        OutputManager* const output_manager)
        : Global(display, Version<1>()),
          wayland_executor{wayland_executor},
          shell{shell},
          seat{seat},
          output_manager{output_manager}
    {
    }

    static auto get_window(wl_resource* window) -> std::shared_ptr<Surface>;

private:
    Executor& wayland_executor;
    std::shared_ptr<msh::Shell> const shell;
    WlSeat& seat;
    OutputManager* const output_manager;

    class Instance : public wayland::Shell
    {
    public:
        Instance(wl_resource* new_resource, WlShell* shell)
            : mw::Shell{new_resource, Version<1>()},
              shell{shell}
        {
        }

    private:
        WlShell* const shell;

        void get_shell_surface(wl_resource* new_shell_surface, wl_resource* surface) override
        {
            new WlShellSurface(
                new_shell_surface,
                shell->wayland_executor,
                WlSurface::from(surface),
                shell->shell,
                shell->seat,
                shell->output_manager);
        }
    };

    void bind(wl_resource* new_resource) override
    {
        new Instance{new_resource, this};
    }
};
}
}

namespace
{
int halt_eventloop(int fd, uint32_t /*mask*/, void* data)
{
    auto display = reinterpret_cast<wl_display*>(data);
    wl_display_terminate(display);

    eventfd_t ignored;
    if (eventfd_read(fd, &ignored) < 0)
    {
        BOOST_THROW_EXCEPTION((std::system_error{
           errno,
           std::system_category(),
           "Failed to consume pause event notification"}));
    }
    return 0;
}
}

namespace
{
void cleanup_display(wl_display *display)
{
    wl_display_flush_clients(display);
    wl_display_destroy(display);
}

std::shared_ptr<mg::GraphicBufferAllocator> allocator_for_display(
    std::shared_ptr<mg::GraphicBufferAllocator> const& buffer_allocator,
    wl_display* display,
    std::shared_ptr<mir::Executor> executor)
{
    try
    {
        buffer_allocator->bind_display(display, std::move(executor));
        return buffer_allocator;
    }
    catch (...)
    {
        std::throw_with_nested(std::runtime_error{"Failed to bind Wayland EGL display"});
    }
}
}

auto mf::create_wl_shell(
    wl_display* display,
    Executor& wayland_executor,
    std::shared_ptr<msh::Shell> const& shell,
    WlSeat* seat,
    OutputManager* const output_manager)
-> std::shared_ptr<void>
{
    return std::make_shared<mf::WlShell>(display, wayland_executor, shell, *seat, output_manager);
}

void mf::WaylandExtensions::init(Context const& context)
{
    custom_extensions(context);
}

void mf::WaylandExtensions::add_extension(std::string const name, std::shared_ptr<void> implementation)
{
    extension_protocols[std::move(name)] = std::move(implementation);
}

void mf::WaylandExtensions::custom_extensions(Context const&)
{
}

auto mir::frontend::WaylandExtensions::get_extension(std::string const& name) const -> std::shared_ptr<void>
{
    auto const result = extension_protocols.find(name);
    if (result != end(extension_protocols))
        return result->second;

    return {};
}

void mf::WaylandExtensions::run_builders(wl_display*, std::function<void(std::function<void()>&& work)> const&)
{
}

mf::WaylandConnector::WaylandConnector(
    std::shared_ptr<msh::Shell> const& shell,
    std::shared_ptr<MirDisplay> const& display_config,
    std::shared_ptr<mi::InputDeviceHub> const& input_hub,
    std::shared_ptr<mi::Seat> const& seat,
    std::shared_ptr<mi::InputDeviceRegistry> const& input_device_registry,
    std::shared_ptr<mg::GraphicBufferAllocator> const& allocator,
    std::shared_ptr<mf::SessionAuthorizer> const& session_authorizer,
    std::shared_ptr<SurfaceStack> const& surface_stack,
    std::shared_ptr<ms::Clipboard> const& clipboard,
    std::shared_ptr<MainLoop> const& main_loop,
    bool arw_socket,
    std::unique_ptr<WaylandExtensions> extensions_,
    WaylandProtocolExtensionFilter const& extension_filter,
    bool enable_key_repeat)
    : display{wl_display_create(), &cleanup_display},
      pause_signal{eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE)},
      executor{std::make_shared<WaylandExecutor>(wl_display_get_event_loop(display.get()))},
      allocator{allocator_for_display(allocator, display.get(), executor)},
      shell{shell},
      extensions{std::move(extensions_)},
      extension_filter{extension_filter}
{
    if (pause_signal == mir::Fd::invalid)
    {
        BOOST_THROW_EXCEPTION((std::system_error{
            errno,
            std::system_category(),
            "Failed to create IPC pause notification eventfd"}));
    }

    if (!display)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{"Failed to create wl_display"});
    }

    wl_display_set_global_filter(display.get(), &wl_display_global_filter_func_thunk, this);

    // Run the builders before creating the seat (because that's what GTK3 expects)
    extensions->run_builders(
        display.get(),
        [executor=executor](std::function<void()>&& work) { executor->spawn(std::move(work)); });

    /*
     * Here be Dragons!
     *
     * Some clients expect a certain order in the publication of globals, and will
     * crash with a different order. Yay!
     *
     * So far I've only found ones which expect wl_compositor before anything else,
     * so stick that first.
     */
    compositor_global = std::make_unique<mf::WlCompositor>(
        display.get(),
        executor,
        std::make_shared<FrameExecutor>(*main_loop),
        this->allocator);
    subcompositor_global = std::make_unique<mf::WlSubcompositor>(display.get());
    seat_global = std::make_unique<mf::WlSeat>(display.get(), input_hub, seat, enable_key_repeat);
    output_manager = std::make_unique<mf::OutputManager>(
        display.get(),
        display_config,
        executor);

    data_device_manager_global = std::make_unique<WlDataDeviceManager>(display.get(), executor, clipboard);

    extensions->init(WaylandExtensions::Context{
        display.get(),
        executor,
        shell,
        clipboard,
        seat_global.get(),
        output_manager.get(),
        surface_stack,
        input_device_registry});

    wl_display_init_shm(display.get());

    char const* wayland_display = nullptr;

    if (auto const display_name = getenv("WAYLAND_DISPLAY"))
    {
        if (wl_display_add_socket(display.get(), display_name) != 0)
        {
            BOOST_THROW_EXCEPTION(
                std::system_error(errno, std::system_category(),
                    "Try unsetting or changing WAYLAND_DISPLAY. The current value can not be used:"));
        }

        wayland_display = display_name;
    }
    else
    {
        wayland_display = wl_display_add_socket_auto(display.get());
    }

    if (wayland_display)
    {
        if (arw_socket)
        {
            chmod((std::string{getenv("XDG_RUNTIME_DIR")} + "/" + wayland_display).c_str(),
                  S_IRUSR|S_IWUSR| S_IRGRP|S_IWGRP | S_IROTH|S_IWOTH);
        };

        this->wayland_display = wayland_display;
    }

    auto wayland_loop = wl_display_get_event_loop(display.get());

    WlClient::setup_new_client_handler(display.get(), shell, session_authorizer, [this](WlClient& client)
        {
            int const fd = wl_client_get_fd(client.raw_client());
            auto const handler_iter = connect_handlers.find(fd);

            if (handler_iter != std::end(connect_handlers))
            {
                auto const callback = handler_iter->second;
                connect_handlers.erase(handler_iter);
                callback(client.client_session());
            }
        });

    pause_source = wl_event_loop_add_fd(wayland_loop, pause_signal, WL_EVENT_READABLE, &halt_eventloop, display.get());
}

mf::WaylandConnector::~WaylandConnector()
{
    try
    {
        allocator->unbind_display(display.get());
    }
    catch (...)
    {
        log(
            logging::Severity::warning,
            MIR_LOG_COMPONENT,
            std::current_exception(),
            "Failed to unbind EGL display");
    }

    if (dispatch_thread.joinable())
    {
        stop();
    }
    wl_event_source_remove(pause_source);
}

void mf::WaylandConnector::start()
{
    dispatch_thread = std::thread{
        [](wl_display* d)
        {
            mir::set_thread_name("Mir/Wayland");
            wl_display_run(d);
        },
        display.get()};

    executor->spawn([this]{ seat_global->server_restart(); });
}

void mf::WaylandConnector::stop()
{
    if (eventfd_write(pause_signal, 1) < 0)
    {
        BOOST_THROW_EXCEPTION((std::system_error{
            errno,
            std::system_category(),
            "Failed to send IPC eventloop pause signal"}));
    }
    if (dispatch_thread.joinable())
    {
        dispatch_thread.join();
        dispatch_thread = std::thread{};
    }
    else
    {
        mir::log_warning("WaylandConnector::stop() called on not-running connector?");
    }
}

int mf::WaylandConnector::client_socket_fd() const
{
    enum { server, client, size };
    int socket_fd[size];

    char const* error = nullptr;

    if (socketpair(AF_LOCAL, SOCK_STREAM, 0, socket_fd))
    {
        error = "Could not create socket pair";
    }
    else
    {
        // TODO: Wait on the result of wl_client_create so we can throw an exception on failure.
        executor->spawn(
                [socket = socket_fd[server], display = display.get()]()
                {
                    if (!wl_client_create(display, socket))
                    {
                        mir::log_error(
                            "Failed to create Wayland client object: %s (errno %i)",
                            strerror(errno),
                            errno);
                    }
                });
    }

    if (error)
        BOOST_THROW_EXCEPTION((std::system_error{errno, std::system_category(), error}));

    return socket_fd[client];
}

int mf::WaylandConnector::client_socket_fd(
    std::function<void(std::shared_ptr<scene::Session> const& session)> const& connect_handler) const
{
    enum { server, client, size };
    int socket_fd[size];

    char const* error = nullptr;

    if (socketpair(AF_LOCAL, SOCK_STREAM, 0, socket_fd))
    {
        error = "Could not create socket pair";
    }
    else
    {
        executor->spawn(
                [socket = socket_fd[server], display = display.get(), this, connect_handler]()
                    {
                        connect_handlers[socket] = std::move(connect_handler);
                        if (!wl_client_create(display, socket))
                        {
                            mir::log_error(
                                "Failed to create Wayland client object: %s (errno %i)",
                                strerror(errno),
                                errno);
                        }
                    });
        // TODO: Wait on the result of wl_client_create so we can throw an exception on failure.
    }

    if (error)
        BOOST_THROW_EXCEPTION((std::system_error{errno, std::system_category(), error}));

    return socket_fd[client];
}

void mf::WaylandConnector::run_on_wayland_display(std::function<void(wl_display*)> const& functor)
{
    executor->spawn([display_ref = display.get(), functor]() { functor(display_ref); });
}

void mf::WaylandConnector::on_surface_created(
    wl_client* client,
    uint32_t id,
    std::function<void(WlSurface*)> const& callback)
{
    compositor_global->on_surface_created(client, id, callback);
}

auto mf::WaylandConnector::socket_name() const -> optional_value<std::string>
{
    return wayland_display;
}

auto mf::WaylandConnector::get_extension(std::string const& name) const -> std::shared_ptr<void>
{
    return extensions->get_extension(name);
}

bool mf::WaylandConnector::wl_display_global_filter_func_thunk(wl_client const* client, wl_global const* global, void *data)
{
    return static_cast<WaylandConnector*>(data)->wl_display_global_filter_func(client, global);
}

bool mf::WaylandConnector::wl_display_global_filter_func(wl_client const* client, wl_global const* global) const
{
    auto const* const interface = wl_global_get_interface(global);
    auto const session = get_session(const_cast<wl_client*>(client));
    return extension_filter(session, interface->name);
}

auto mir::frontend::get_session(wl_client* wl_client) -> std::shared_ptr<scene::Session>
{
    auto const client = WlClient::from(wl_client);
    return client ? client->client_session() : nullptr;
}

auto mf::get_session(wl_resource* surface) -> std::shared_ptr<ms::Session>
{
    // TODO: evaluate if this is actually what we want. Sometime's a surface's client's session is not the most
    // applicable session for the surface. See WlClient::client_session for details.
    return get_session(wl_resource_get_client(surface));
}

auto mf::get_wl_shell_window(wl_resource* surface) -> std::shared_ptr<ms::Surface>
{
    if (mw::Surface::is_instance(surface))
    {
        auto const wlsurface = WlSurface::from(surface);
        if (auto const scene_surface = wlsurface->scene_surface())
        {
            return scene_surface.value();
        }

        log_debug("No window currently associated with wayland::Surface %p", static_cast<void*>(surface));
    }

    return {};
}
