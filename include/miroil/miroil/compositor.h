/*
 * Copyright © 2021 Canonical Ltd.
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
 */

#ifndef MIROIL_COMPOSITOR_H
#define MIROIL_COMPOSITOR_H

namespace miroil
{

class Compositor
{
    public:
    virtual ~Compositor();

    Compositor& operator=(Compositor const&) = delete;

    virtual void start() = 0;
    virtual void stop()  = 0;

protected:
    Compositor() = default;
    Compositor(Compositor const&) = delete;
};

}

#endif // MIROIL_COMPOSITOR_H
