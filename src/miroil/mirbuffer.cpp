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

#include "miroil/mirbuffer.h"

#include <mir/graphics/buffer.h>
#include <mir/graphics/texture.h>

#if MIR_SERVER_VERSION < MIR_VERSION_NUMBER(2, 3, 0)
#include <mir/gl/texture.h>
#include <mir/renderer/gl/texture_source.h>
#endif

#include <stdexcept>

miroil::GLBuffer::GLBuffer() = default;
miroil::GLBuffer::~GLBuffer()
{
    destroy();
}

miroil::GLBuffer::GLBuffer(std::shared_ptr<mir::graphics::Buffer> const& buffer) :
    wrapped(buffer),
    m_textureId(0)
#if MIR_SERVER_VERSION < MIR_VERSION_NUMBER(2, 3, 0)
    ,
    m_isOldTex(false)
#endif
{
    init();
}

void miroil::GLBuffer::init()
{
#if MIR_SERVER_VERSION < MIR_VERSION_NUMBER(2, 3, 0)
    if (m_inited)
        return;

    if (!dynamic_cast<mir::graphics::gl::Texture*>(wrapped->native_buffer_base()))
    {
        glGenTextures(1, &m_textureId);
        m_isOldTex = true;
    }

    m_inited = true;
#endif
}

void miroil::GLBuffer::destroy()
{
    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
#if MIR_SERVER_VERSION < MIR_VERSION_NUMBER(2, 3, 0)
        m_isOldTex = false;
#endif
    }
}

void miroil::GLBuffer::reset(std::shared_ptr<mir::graphics::Buffer> const& buffer)
{
    wrapped = buffer;
}

miroil::GLBuffer::operator bool() const
{
    return !!wrapped;
}

#if MIR_SERVER_VERSION < MIR_VERSION_NUMBER(2, 3, 0)
bool miroil::GLBuffer::has_alpha_channel() const
{
    return wrapped &&
        (wrapped->pixel_format() == mir_pixel_format_abgr_8888
        || wrapped->pixel_format() == mir_pixel_format_argb_8888);
}
#endif

mir::geometry::Size miroil::GLBuffer::size() const
{
    return wrapped->size();
}

void miroil::GLBuffer::reset()
{
    wrapped.reset();
}

#if MIR_SERVER_VERSION < MIR_VERSION_NUMBER(2, 3, 0)
void miroil::GLBuffer::gl_bind_tex()
{
    if (m_isOldTex)
        glBindTexture(GL_TEXTURE_2D, m_textureId);
}
#endif

void miroil::GLBuffer::bind()
{
#if MIR_SERVER_VERSION < MIR_VERSION_NUMBER(2, 3, 0)
    if (m_isOldTex) {
        auto const texsource = dynamic_cast<mir::renderer::gl::TextureSource*>(wrapped->native_buffer_base());

        texsource->gl_bind_to_texture();
        texsource->secure_for_render();
        return;
    }
#endif
    if (auto const texture = dynamic_cast<mir::graphics::gl::Texture*>(wrapped->native_buffer_base()))
    {
        texture->bind();
    }
    else
    {
        throw std::logic_error("Buffer does not support GL rendering");
    }
}
