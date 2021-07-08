/*
 * Copyright © 2016-2020 Canonical Ltd.
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

#ifndef MIROIL_PERSIST_DISPLAY_CONFIG_H
#define MIROIL_PERSIST_DISPLAY_CONFIG_H

#include <functional>
#include <memory>

namespace mir { class Server; namespace graphics { class DisplayConfigurationPolicy; }}

namespace miroil
{
class DisplayConfigurationPolicy;
class DisplayConfigurationStorage;

/// Restores the saved display configuration and saves changes to the base configuration
class PersistDisplayConfig
{
public:
    ~PersistDisplayConfig();
    PersistDisplayConfig(PersistDisplayConfig const&);
    auto operator=(PersistDisplayConfig const&) -> PersistDisplayConfig&;

    // TODO factor this out better
    using DisplayConfigurationPolicyWrapper = std::function<std::shared_ptr<DisplayConfigurationPolicy>(std::shared_ptr<mir::graphics::DisplayConfigurationPolicy> const& wrapped)>;

    PersistDisplayConfig(std::shared_ptr<DisplayConfigurationStorage> const& storage,
                         DisplayConfigurationPolicyWrapper const& custom_wrapper);

    void operator()(mir::Server& server);

private:
    struct Self;
    std::shared_ptr<Self> self;
};
}

#endif //MIROIL_PERSIST_DISPLAY_CONFIG_H
