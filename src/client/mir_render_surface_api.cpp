/*
 * Copyright © 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
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
 * Authored by: Cemil Azizoglu <cemil.azizoglu@canonical.com>
 */

#include "mir_connection.h"
#include "render_surface.h"
#include "mir/uncaught.h"
#include "mir/require.h"
#include "connection_surface_map.h"

namespace
{
// assign_result is compatible with all 2-parameter callbacks
void assign_result(void* result, void** context)
{
    if (context)
        *context = result;
}
}

namespace
{
// 'Native' render surface to connection
class RenderSurfaceToConnectionMap
{
public:
    void insert(void* render_surface_key, MirConnection* connection)
    {
        std::lock_guard<decltype(guard)> lk(guard);
        connections[render_surface_key] = connection;
    }

    void erase(void* render_surface_key)
    {
        std::lock_guard<decltype(guard)> lk(guard);
        auto conn_it = connections.find(render_surface_key);
        if (conn_it != connections.end())
            connections.erase(conn_it);
    }

    MirConnection* connection(void* render_surface_key) const
    {
        std::shared_lock<decltype(guard)> lk(guard);
        auto const it = connections.find(render_surface_key);
        if (it != connections.end())
            return it->second;
        else
            BOOST_THROW_EXCEPTION(std::runtime_error("could not find matching connection"));
    }
private:
    std::shared_timed_mutex mutable guard;
    std::unordered_map<void*, MirConnection*> connections;
};

RenderSurfaceToConnectionMap connection_map;
}

MirRenderSurface* mir_connection_create_render_surface(
    MirConnection* connection,
    int const width, int const height,
    MirPixelFormat const format,
    MirBufferUsage usage)
try
{
    mir::require(connection);
    auto rs = connection->create_render_surface(width, height, format, usage);
    connection_map.insert(static_cast<void*>(rs), connection);
    return rs;
}
catch (std::exception const& ex)
{
    MIR_LOG_UNCAUGHT_EXCEPTION(ex);
    return nullptr;
}

bool mir_render_surface_is_valid(
    MirRenderSurface* render_surface)
try
{
    mir::require(render_surface &&
                 connection_map.connection(static_cast<void*>(render_surface))->connection_surface_map()->render_surface(render_surface));
    return true;
}
catch (std::exception const& ex)
{
    MIR_LOG_UNCAUGHT_EXCEPTION(ex);
    return false;
}

void mir_render_surface_release(
    MirRenderSurface* render_surface)
try
{
    mir::require(render_surface);
    auto connection = connection_map.connection(static_cast<void*>(render_surface));
    connection_map.erase(static_cast<void*>(render_surface));
    connection->release_render_surface(render_surface);
}
catch (std::exception const& ex)
{
    MIR_LOG_UNCAUGHT_EXCEPTION(ex);
}

MirWaitHandle* mir_render_surface_create_buffer_stream(
    MirRenderSurface* render_surface,
    mir_buffer_stream_callback callback,
    void* context)
try
{
    mir::require(render_surface);
    auto connection = connection_map.connection(static_cast<void*>(render_surface));
    auto rs = connection->connection_surface_map()->render_surface(render_surface);
    return rs->create_client_buffer_stream(callback, context);
}
catch (std::exception const& ex)
{
    MIR_LOG_UNCAUGHT_EXCEPTION(ex);
    return nullptr;
}

MirBufferStream* mir_render_surface_create_buffer_stream_sync(
    MirRenderSurface* render_surface)
{
    MirBufferStream* stream = nullptr;
    mir_render_surface_create_buffer_stream(render_surface,
        reinterpret_cast<mir_buffer_stream_callback>(assign_result), &stream)->wait_for_all();
    return stream;
}
