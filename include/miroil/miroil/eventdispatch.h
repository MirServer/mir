/*
 * Copyright © 2017 Canonical Ltd.
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

#ifndef MIROIL_EVENTDISPATCH_H
#define MIROIL_EVENTDISPATCH_H

namespace miral { class Window; }

struct MirInputEvent;

namespace miroil
{
void dispatch_input_event(const miral::Window& window, const MirInputEvent* event);
}

#endif //MIROIL_EVENTDISPATCH_H
