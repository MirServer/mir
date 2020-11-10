/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by:
 *   Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "mir/graphics/egl_extensions.h"
#include <boost/throw_exception.hpp>
#include <stdexcept>
#include <cstring>

#define MIR_LOG_COMPONENT "EGL extensions"
#include "mir/log.h"

namespace mg=mir::graphics;

namespace
{

std::experimental::optional<mg::EGLExtensions::PlatformBaseEXT> maybe_platform_base_ext()
{
    try
    {
        return mg::EGLExtensions::PlatformBaseEXT{};
    }
    catch (std::runtime_error const&)
    {
        return {};
    }
}

}

mg::EGLExtensions::EGLExtensions() :
    eglCreateImageKHR{
        reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"))},
    eglDestroyImageKHR{
        reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"))},

    /*
     * TODO: Find a non-ES GL equivalent for glEGLImageTargetTexture2DOES
     * It's the LAST remaining ES-specific function. Although Mesa lets you use
     * it in full GL, it theoretically should not work. Mesa just lets you
     * mix ES and GL code. But other drivers won't be so lenient.
     */
    glEGLImageTargetTexture2DOES{
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"))},
    platform_base{maybe_platform_base_ext()}
{
    if (!eglCreateImageKHR || !eglDestroyImageKHR)
        BOOST_THROW_EXCEPTION(std::runtime_error("EGL implementation doesn't support EGLImage"));

    if (!glEGLImageTargetTexture2DOES)
        BOOST_THROW_EXCEPTION(std::runtime_error("GLES2 implementation doesn't support updating a texture from an EGLImage"));
}

mg::EGLExtensions::WaylandExtensions::WaylandExtensions(EGLDisplay dpy) :
    eglBindWaylandDisplayWL{
        reinterpret_cast<PFNEGLBINDWAYLANDDISPLAYWL>(eglGetProcAddress("eglBindWaylandDisplayWL"))
    },
    eglUnbindWaylandDisplayWL{
        reinterpret_cast<PFNEGLBINDWAYLANDDISPLAYWL>(eglGetProcAddress("eglUnbindWaylandDisplayWL"))
    },
    eglQueryWaylandBufferWL{
        reinterpret_cast<PFNEGLQUERYWAYLANDBUFFERWL>(eglGetProcAddress("eglQueryWaylandBufferWL"))
    }
{
    auto const egl_extensions = eglQueryString(dpy, EGL_EXTENSIONS);
    if (!egl_extensions || !strstr(egl_extensions, "EGL_WL_bind_wayland_display"))
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("EGL display doesn't support EGL_WL_bind_wayland_display"));
    }

    if (!eglBindWaylandDisplayWL || !eglUnbindWaylandDisplayWL || !eglQueryWaylandBufferWL)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("EGL_WL_bind_wayland_display functions are null"));
    }
}

mg::EGLExtensions::NVStreamAttribExtensions::NVStreamAttribExtensions() :
    eglCreateStreamAttribNV{
        reinterpret_cast<PFNEGLCREATESTREAMATTRIBNVPROC>(eglGetProcAddress("eglCreateStreamAttribNV"))
    },
    eglStreamConsumerAcquireAttribNV{
        reinterpret_cast<PFNEGLSTREAMCONSUMERACQUIREATTRIBNVPROC>(
            eglGetProcAddress("eglStreamConsumerAcquireAttribNV"))
    }
{
    if (!eglCreateStreamAttribNV || !eglStreamConsumerAcquireAttribNV)
    {
        BOOST_THROW_EXCEPTION((std::runtime_error{"EGL implementation doesn't support EGL_NV_stream_attrib"}));
    }
}

mg::EGLExtensions::PlatformBaseEXT::PlatformBaseEXT()
    : eglGetPlatformDisplay{
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"))
    },
    eglCreatePlatformWindowSurface{
          reinterpret_cast<PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC>(
              eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT"))
    }
{
    auto const* client_extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!client_extensions ||
        !strstr(client_extensions, "EGL_EXT_platform_base") ||
        !eglGetPlatformDisplay ||
        !eglCreatePlatformWindowSurface)
    {
        BOOST_THROW_EXCEPTION((std::runtime_error{"EGL implementation doesn't support EGL_EXT_platform_base"}));
    }
}

mg::EGLExtensions::DebugKHR::DebugKHR()
    : eglDebugMessageControlKHR{
        reinterpret_cast<PFNEGLDEBUGMESSAGECONTROLKHRPROC>(eglGetProcAddress("eglDebugMessageControlKHR"))
    },
      eglLabelObjectKHR{
        reinterpret_cast<PFNEGLLABELOBJECTKHRPROC>(eglGetProcAddress("eglLabelObjectKHR"))
    },
      eglQueryDebugKHR{
        reinterpret_cast<PFNEGLQUERYDEBUGKHRPROC>(eglGetProcAddress("eglQueryDebugKHR"))
    }
{
    auto const* client_extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!client_extensions ||
        !strstr(client_extensions, "EGL_KHR_debug") ||
        !eglDebugMessageControlKHR ||
        !eglLabelObjectKHR ||
        !eglQueryDebugKHR)
    {
        BOOST_THROW_EXCEPTION((std::runtime_error{"EGL implementation doesn't support EGL_KHR_debug"}));
    }
}

mg::EGLExtensions::DebugKHR::DebugKHR(
    PFNEGLDEBUGMESSAGECONTROLKHRPROC control,
    PFNEGLLABELOBJECTKHRPROC label,
    PFNEGLQUERYDEBUGKHRPROC query)
    : eglDebugMessageControlKHR{control},
      eglLabelObjectKHR{label},
      eglQueryDebugKHR{query}
{
}

auto mg::EGLExtensions::DebugKHR::extension_or_null_object() -> DebugKHR
{
    return DebugKHR{
        [](EGLDEBUGPROCKHR, EGLAttrib const*) -> EGLint { return EGL_SUCCESS; },
        [](EGLDisplay, EGLenum, EGLObjectKHR, EGLLabelKHR) -> EGLint { return EGL_SUCCESS; },
        [](EGLint, EGLAttrib*) -> EGLBoolean { return EGL_TRUE; }
    };
}

auto mg::EGLExtensions::DebugKHR::maybe_debug_khr() -> std::experimental::optional<DebugKHR>
{
    try
    {
        return mg::EGLExtensions::DebugKHR{};
    }
    catch (std::runtime_error const&)
    {
        return {};
    }
}

mg::EGLExtensions::EXTImageDmaBufImportModifiers::EXTImageDmaBufImportModifiers(EGLDisplay dpy)
    : eglQueryDmaBufFormatsExt{
        reinterpret_cast<PFNEGLQUERYDMABUFFORMATSEXTPROC>(
            eglGetProcAddress("eglQueryDmaBufFormatsEXT"))},
      eglQueryDmaBufModifiersExt{
        reinterpret_cast<PFNEGLQUERYDMABUFMODIFIERSEXTPROC>(
            eglGetProcAddress("eglQueryDmaBufModifiersEXT"))}
{
    auto const egl_extensions = eglQueryString(dpy, EGL_EXTENSIONS);
    if (!egl_extensions ||
        !strstr(egl_extensions, "EGL_EXT_image_dma_buf_import_modifiers"))
    {
        BOOST_THROW_EXCEPTION((
            std::runtime_error{"EGL_EXT_image_dma_buf_import_modifiers not supported"}));
    }
}
