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
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#include "miral/wayland_extensions.h"
#include "miral/window.h"

#include <mir/abnormal_exit.h>
#include <mir/frontend/wayland.h>
#include <mir/scene/session.h>
#include <mir/scene/surface.h>
#include <mir/server.h>
#include <mir/options/option.h>
#include <mir/options/configuration.h>

#include <algorithm>
#include <mutex>
#include <set>
#include <vector>

namespace mo = mir::options;

char const* const miral::WaylandExtensions::zwlr_layer_shell_v1{"zwlr_layer_shell_v1"};
char const* const miral::WaylandExtensions::zxdg_output_manager_v1{"zxdg_output_manager_v1"};
char const* const miral::WaylandExtensions::zwlr_foreign_toplevel_manager_v1{"zwlr_foreign_toplevel_manager_v1"};
char const* const miral::WaylandExtensions::zwp_virtual_keyboard_manager_v1{"zwp_virtual_keyboard_manager_v1"};

namespace
{
auto vec2set(std::vector<std::string> vec) -> std::set<std::string>
{
    return std::set<std::string>{vec.begin(), vec.end()};
}

// If a user inadvertently supplies more than one miral::WaylandExtensions to
// MirRunner::run_with() then the implementation here would quietly break.
// To avoid this we track the extensions against all servers in the process.
// See: https://github.com/MirServer/mir/issues/875
struct StaticExtensionTracker
{
    static void add_server_extension(mir::Server* server, void* extension)
    {
        std::lock_guard<decltype(mutex)> lock{mutex};

        for (auto const& x : extensions)
        {
            if (x.server == server)
            {
                if (x.extension == extension)
                {
                    return;
                }
                else
                {
                    throw mir::AbnormalExit{"ERROR: multiple miral::WaylandExtensions registered with server"};
                }
            }
        }
        extensions.push_back({server, extension});
    }

    static void remove_extension(void* extension)
    {
        std::lock_guard<decltype(mutex)> lock{mutex};

        extensions.erase(
            remove_if(begin(extensions), end(extensions), [extension](auto& x) { return x.extension == extension; }),
            end(extensions));
    }

private:
    // It's extremely unlikely that there's more than a couple, so lock contention
    // is a non-issue and a vector is a suitable data.
    struct ExtensionTracker{ mir::Server* server; void* extension; };
    static std::mutex mutex;
    static std::vector<ExtensionTracker> extensions;
};

decltype(StaticExtensionTracker::mutex)       StaticExtensionTracker::mutex;
decltype(StaticExtensionTracker::extensions)  StaticExtensionTracker::extensions;
}

struct miral::WaylandExtensions::Self
{

    Self()
        : default_extensions{WaylandExtensions::recommended()},
          supported_extensions{WaylandExtensions::supported()}
    {
    }

    ~Self()
    {
        StaticExtensionTracker::remove_extension(this);
    }

    static auto parse_extensions_option(std::string extensions) -> std::set<std::string>
    {
        std::set<std::string> extension;
        extensions += ':';

        for (char const* start = extensions.c_str(); char const* end = strchr(start, ':'); start = end+1)
        {
            if (start != end)
                extension.insert(std::string{start, end});
        }

        return extension;
    }

    static auto serialize_colon_list(std::vector<std::string> extensions) -> std::string
    {
        bool at_start = true;
        std::string result;
        for (auto extension : extensions)
        {
            if (at_start)
                at_start = false;
            else
                result += ":";
            result += extension;
        }
        return result;
    }

    void validate(std::set<std::string> extensions) const
    {
        std::vector<std::string> errors;

        set_difference(
            begin(extensions), end(extensions),
            begin(supported_extensions), end(supported_extensions),
            std::inserter(errors, begin(errors)));

        if (!errors.empty())
        {
            throw mir::AbnormalExit{
                "Unsupported wayland extensions: " +
                serialize_colon_list(errors) +
                ". Supported extensions are: " +
                serialize_colon_list({begin(supported_extensions), end(supported_extensions)})};
        }
    }

    void add_extension(Builder const& builder)
    {
        wayland_extension_hooks.push_back(builder);
        supported_extensions.insert(builder.name);
        printf("%s = %s\n", __PRETTY_FUNCTION__, builder.name.c_str());
    }

    void enable_extension(std::string name)
    {
        if (supported_extensions.find(name) == supported_extensions.end())
            BOOST_THROW_EXCEPTION(std::runtime_error("Attempted to enable unsupported extension " + name));
        else
            default_extensions.insert(name);
    }

    void disable_extension(std::string name)
    {
        if (supported_extensions.find(name) == supported_extensions.end())
            BOOST_THROW_EXCEPTION(std::runtime_error("Attempted to disable unsupported extension " + name));
        else
            default_extensions.erase(name);
    }

    std::vector<Builder> wayland_extension_hooks;
    /**
     * Extensions to enable by default if the user does not override with a command line options
     * This starts set to mir::frontend::get_standard_extensions()
     */
    std::set<std::string> default_extensions;
    /**
     * All the extensions this shell supports
     * This includes extensions returned by mir::frontend::get_supported_extensions() and any bespoke extensions added
     */
    std::set<std::string> supported_extensions;
    WaylandExtensions::Filter extensions_filter = [](Application const&, char const*) { return true; };
};


miral::WaylandExtensions::WaylandExtensions()
    : self{std::make_shared<Self>()}
{
}

void miral::WaylandExtensions::operator()(mir::Server& server) const
{
    StaticExtensionTracker::add_server_extension(&server, self.get());

    std::vector<std::string> supported_extensions{self->supported_extensions.begin(), self->supported_extensions.end()};
    std::vector<std::string> default_extensions{self->default_extensions.begin(), self->default_extensions.end()};
    std::vector<std::string> non_default_extensions;
    for (auto const& extension : self->supported_extensions)
    {
        if (self->default_extensions.find(extension) == self->default_extensions.end())
        {
            non_default_extensions.push_back(extension);
        }
    }

    server.add_configuration_option(
        mo::wayland_extensions_opt,
        ("Exhaustive list of all Wayland extensions to enable. [" + Self::serialize_colon_list(supported_extensions) + "]"),
        mir::OptionType::string);

    server.add_configuration_option(
        mo::add_wayland_extensions_opt,
        ("Additional Wayland extensions to enable. [" + Self::serialize_colon_list(non_default_extensions) + "]"),
        mir::OptionType::string);

    server.add_configuration_option(
        mo::drop_wayland_extensions_opt,
        ("Wayland extensions to disable. [" + Self::serialize_colon_list(default_extensions) + "]"),
        mir::OptionType::string);

    server.add_pre_init_callback([self=self, &server]
        {
            for (auto const& hook : self->wayland_extension_hooks)
            {
                struct FrigContext : Context
                {
                    wl_display* display_ = nullptr;
                    std::function<void(std::function<void()>&& work)> executor;

                    wl_display* display() const override
                    {
                        return display_;
                    }

                    void run_on_wayland_mainloop(std::function<void()>&& work) const override
                    {
                        executor(std::move(work));
                    }
                };

                auto frig = [build=hook.build, context=std::make_shared<FrigContext>()]
                    (wl_display* display, std::function<void(std::function<void()>&& work)> const& executor)
                {
                    context->display_ = display;
                    context->executor = executor;
                    return build(context.get());
                };

                server.add_wayland_extension(hook.name, std::move(frig));
            }

            server.set_wayland_extension_filter(self->extensions_filter);

            std::set<std::string> selected_extensions;
            if (server.get_options()->is_set(mo::wayland_extensions_opt))
            {
                selected_extensions = Self::parse_extensions_option(
                    server.get_options()->get<std::string>(mo::wayland_extensions_opt));
            }
            else
            {
                selected_extensions = self->default_extensions;
            }

            if (server.get_options()->is_set(mo::add_wayland_extensions_opt))
            {
                auto const added = Self::parse_extensions_option(
                    server.get_options()->get<std::string>(mo::add_wayland_extensions_opt));
                for (auto const& extension : added)
                {
                    selected_extensions.insert(extension);
                }
            }

            if (server.get_options()->is_set(mo::drop_wayland_extensions_opt))
            {
                auto const dropped = Self::parse_extensions_option(
                    server.get_options()->get<std::string>(mo::drop_wayland_extensions_opt));
                for (auto const& extension : dropped)
                {
                    selected_extensions.erase(extension);
                }
            }

            self->validate(selected_extensions);
            server.set_enabled_wayland_extensions(
                std::vector<std::string>{
                    selected_extensions.begin(),
                    selected_extensions.end()});
        });
}

miral::WaylandExtensions::~WaylandExtensions() = default;
miral::WaylandExtensions::WaylandExtensions(WaylandExtensions const&) = default;
auto miral::WaylandExtensions::operator=(WaylandExtensions const&) -> WaylandExtensions& = default;

void miral::WaylandExtensions::add_extension(Builder const& builder)
{
    self->add_extension(builder);
    self->enable_extension(builder.name);
}

void miral::WaylandExtensions::set_filter(miral::WaylandExtensions::Filter const& extension_filter)
{
    // Wayland calls the filter for all protocols (not just the optional extensions).
    // To avoid accidents (like denying base protocols) we only defer to the provided
    // extension_filter for supported_extensions.
    self->extensions_filter = [&optional = self->supported_extensions, extension_filter]
        (Application const& app, char const* protocol)
        {
            return (optional.count(protocol) == 0) || extension_filter(app, protocol);
        };
}

void miral::WaylandExtensions::add_extension_disabled_by_default(miral::WaylandExtensions::Builder const& builder)
{
    self->add_extension(builder);
}

auto miral::WaylandExtensions::recommended() -> std::set<std::string>
{
    return vec2set(mir::frontend::get_standard_extensions());
}

auto miral::WaylandExtensions::supported() -> std::set<std::string>
{
    return vec2set(mir::frontend::get_supported_extensions());
}

auto miral::WaylandExtensions::enable(std::string name) -> WaylandExtensions&
{
    self->enable_extension(name);
    return *this;
}

auto miral::WaylandExtensions::disable(std::string name) -> WaylandExtensions&
{
    self->disable_extension(name);
    return *this;
}

auto miral::WaylandExtensions::all_supported() const -> std::set<std::string>
{
    return self->supported_extensions;
}

auto miral::application_for(wl_client* client) -> Application
{
    return mir::frontend::get_session(client);
}

auto miral::application_for(wl_resource* resource) -> Application
{
    return mir::frontend::get_session(resource);
}

auto miral::window_for(wl_resource* surface) -> Window
{
    if (auto const& app = application_for(surface))
    {
        return {app, std::dynamic_pointer_cast<mir::scene::Surface>(mir::frontend::get_window(surface))};
    }
    else
    {
        return {};
    }
}
