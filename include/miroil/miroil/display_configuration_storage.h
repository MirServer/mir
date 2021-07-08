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
 * Authored by: Nick Dedekind <nick.dedekind@canonical.com>
 */

#ifndef MIROIL_DISPLAY_CONFIGURATION_STORAGE_H
#define MIROIL_DISPLAY_CONFIGURATION_STORAGE_H

#include <miroil/display_id.h>

#include <mir/geometry/rectangle.h>
#include <mir/optional_value.h>
#include <mir_toolkit/common.h>

#include <sys/types.h>

namespace miroil
{

struct DisplayConfigurationOptions
{
    mir::optional_value<bool> used;
    mir::optional_value<uint> clone_output_index;
    struct DisplayMode
    {
        mir::geometry::Size size;
        double refresh_rate{-1};
    };
    mir::optional_value<DisplayMode> mode;
    mir::optional_value<MirOrientation> orientation;
    mir::optional_value<MirFormFactor> form_factor;
    mir::optional_value<float> scale;
};

class DisplayConfigurationStorage
{
public:
    virtual ~DisplayConfigurationStorage() = default;

    virtual void save(const DisplayId&, const DisplayConfigurationOptions&) = 0;
    virtual bool load(const DisplayId&, DisplayConfigurationOptions&) const = 0;
};

} // namespace miroil

#endif // MIROIL_DISPLAY_CONFIGURATION_STORAGE_H
