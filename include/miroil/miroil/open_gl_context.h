/*
 * Copyright © 2016 Canonical Ltd.
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

#ifndef MIROIL_OPEN_GL_CONTEXT_H
#define MIROIL_OPEN_GL_CONTEXT_H
#include <mir/graphics/gl_config.h>
#include <memory>
#include <functional>

namespace mir { class Server; }

namespace miroil
{
class OpenGLContext
{
public:
    OpenGLContext(mir::graphics::GLConfig* gl_config);

    void operator()(mir::Server& server);
    auto the_open_gl_config() const -> std::shared_ptr<mir::graphics::GLConfig>;

private:
    struct Self;
    std::shared_ptr<Self> self;
};
}

#endif //MIROIL_OPEN_GL_CONTEXT_H
