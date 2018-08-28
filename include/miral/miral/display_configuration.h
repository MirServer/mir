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

#ifndef MIRAL_DISPLAY_CONFIGURATION_H
#define MIRAL_DISPLAY_CONFIGURATION_H

#include <memory>
#include <string>
#include <vector>

namespace mir { class Server; }

namespace miral
{
/// Enable display configuration.
/// The config file (<server-name>.display) is located via the XDG Base
/// Directory Specification. Vis:
///($XDG_CONFIG_HOME or $HOME/.config followed by $XDG_CONFIG_DIRS)
/// \remark Since MirAL 2.4
class DisplayConfiguration
{
public:
    DisplayConfiguration();

    /// Select a layout from the configuration
    void select_layout(std::string const& layout);

    void operator()(mir::Server& server) const;

    ~DisplayConfiguration();
    DisplayConfiguration(DisplayConfiguration const&);
    auto operator=(DisplayConfiguration const&) -> DisplayConfiguration&;

private:
    struct Self;
    std::shared_ptr<Self> self;
};
}

#endif //MIRAL_DISPLAY_CONFIGURATION_H
