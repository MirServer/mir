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
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#include "miral/display_configuration.h"
#include "static_display_config.h"
#include "shared_data.h"

#include <mir/server.h>

#include <fstream>
#include <sstream>

struct miral::DisplayConfiguration::Self : StaticDisplayConfig {};

miral::DisplayConfiguration::DisplayConfiguration() :
    self{std::make_shared<Self>()}
{
}

void miral::DisplayConfiguration::select_layout(std::string const& layout)
{
    self->select_layout(layout);
}

void miral::DisplayConfiguration::operator()(mir::Server& server) const
{
    std::string const name = rootname + ".display";
    std::string config_roots;

    if (auto config_home = getenv("XDG_CONFIG_HOME"))
        (config_roots = config_home) += ":";
    else if (auto home = getenv("HOME"))
        (config_roots = home) += "/.config:";

    if (auto config_dirs = getenv("XDG_CONFIG_DIRS"))
        config_roots += config_dirs;
    else
        config_roots += "/etc/xdg";

    std::istringstream config_stream(config_roots);

    /* Read options from config files */
    for (std::string config_root; getline(config_stream, config_root, ':');)
    {
        auto const& filename = config_root + "/" + name;

        if (std::ifstream config_file{filename})
        {
            self->load_config(config_file, "ERROR: in display configuration file: '" + filename + "' : ");
            break;
        }
    }

    namespace mg = mir::graphics;

    server.wrap_display_configuration_policy([this](std::shared_ptr<mg::DisplayConfigurationPolicy> const&)
        -> std::shared_ptr<mg::DisplayConfigurationPolicy>
        {
            return self;
        });
}

miral::DisplayConfiguration::~DisplayConfiguration() = default;

miral::DisplayConfiguration::DisplayConfiguration(miral::DisplayConfiguration const&) = default;

auto miral::DisplayConfiguration::operator=(miral::DisplayConfiguration const&) -> miral::DisplayConfiguration& = default;
