/*
 * Copyright © 2017-2020 Canonical Ltd.
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

#ifndef MIROIL_GLBUFFER_H
#define MIROIL_GLBUFFER_H

#include <mir/geometry/size.h>
#include <mir/version.h>
#include <memory>

#include <GL/gl.h>

namespace mir { namespace graphics { class Buffer; }}

namespace miroil
{
class GLBuffer
{
public:
    GLBuffer();
    ~GLBuffer();
    explicit GLBuffer(std::shared_ptr<mir::graphics::Buffer> const& buffer);

    operator bool() const;
#if MIR_SERVER_VERSION < MIR_VERSION_NUMBER(2, 3, 0)
    bool has_alpha_channel() const;
#endif
    mir::geometry::Size size() const;

    void reset();
    void reset(std::shared_ptr<mir::graphics::Buffer> const& buffer);
    void bind();
#if MIR_SERVER_VERSION < MIR_VERSION_NUMBER(2, 3, 0)
    void gl_bind_tex();
#endif

private:
    void init();
    void destroy();

    std::shared_ptr<mir::graphics::Buffer> wrapped;
    GLuint m_textureId;
#if MIR_SERVER_VERSION < MIR_VERSION_NUMBER(2, 3, 0)
    bool m_isOldTex = false;
    bool m_inited = false;
#endif
};
}

#endif //MIROIL_GLBUFFER_H
