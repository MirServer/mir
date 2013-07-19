/*
 * Copyright © 2013 Canonical Ltd.
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
 * Authored by: Daniel van Vugt <daniel.van.vugt@canonical.com>
 *
 *
 * The "bundle" of buffers is actually a ring (circular array) from which
 * buffers are allocated and progress through stages.
 *
 * The stages of a buffer are:
 *   free -> client -> ready -> compositor -> free
 *                     ready (dropped)-> free
 *
 * Dropping only happens when it's enabled, and only if the ring is
 * completely full.
 *
 * The successive stages are contiguous elements in the ring (starting at
 * element "first_compositor"):
 *    first_compositor * ncompositors(zero or more)
 *    first_ready      * nready(zero or more)
 *    first_client     * nclients(zero or more)
 *
 * Therefore you will find:
 *    first_compositor + ncompositors == first_ready
 *    first_ready + nready == first_client
 * although the ring wraps around, so all addition is modulo (%) nbuffers.
 *
 * "free" is an implicit state for any buffer that is not in any of the
 * above three groups. So the next free buffer is always:
 *    first_client + nclients
 * and free buffers extend up to but not including first_compositor.
 *
 *  |<--------------------- nbuffers ----------------------->|
 *             | ncompos |    nready    | nclients|
 *  +----------+---------+--------------+---------+----------+
 *  | ... free |    |    |    |    |    |    |    | free ... |
 *  +----------+---------+--------------+---------+----------+
 *               ^         ^ first_ready  ^ first_client
 *               first_compositor
 */

#include "mir/compositor/graphic_buffer_allocator.h"
#include "switching_bundle.h"
#include <cassert>

namespace mc=mir::compositor;
namespace mg = mir::graphics;

mc::SwitchingBundle::SwitchingBundle(
    std::shared_ptr<GraphicBufferAllocator> &gralloc,
    BufferProperties const& property_request)
    : bundle_properties{property_request},
      gralloc{gralloc},
      nbuffers{2},
      first_compositor{0}, ncompositors{0},
      first_ready{0}, nready{0},
      first_client{0}, nclients{0},
      framedropping{false}
{
    assert(nbuffers > 0 && nbuffers <= MAX_BUFFERS);
    for (int i = 0; i < MAX_BUFFERS; i++)
        ring[i] = gralloc->alloc_buffer(bundle_properties);
}

int mc::SwitchingBundle::drop_frames(int max)
{
    int drop = max;

    if (drop > nready)
        drop = nready;

    for (int d = 0; d < drop; d++)
    {
        first_client = (first_client + nbuffers - 1) % nbuffers;
        nready--;
        auto tmp = ring[first_client];
        int end = (first_client + nclients) % nbuffers;

        for (int i = first_client; i != end; i = (i + 1) % nbuffers)
            ring[i] = ring[(i + 1) % nbuffers];

        ring[end] = tmp;
    }

    return drop;
}

std::shared_ptr<mg::Buffer> mc::SwitchingBundle::client_acquire()
{
    std::unique_lock<std::mutex> lock(guard);
    int client;

    if (framedropping && nbuffers > 1)
    {
        if (ncompositors + nready + nclients >= nbuffers)
        {
            while (nready == 0)
                cond.wait(lock);

            drop_frames(1);
        }
    }
    else
    {
        while (ncompositors + nready + nclients >= nbuffers)
            cond.wait(lock);
    }

    client = (first_client + nclients) % nbuffers;
    nclients++;

    return ring[client];
}

void mc::SwitchingBundle::client_release(std::shared_ptr<mg::Buffer> const& released_buffer)
{
    std::unique_lock<std::mutex> lock(guard);
    assert(ring[first_client] == released_buffer);
    first_client = (first_client + 1) % nbuffers;
    nclients--;
    nready++;
    cond.notify_all();
}

std::shared_ptr<mg::Buffer> mc::SwitchingBundle::compositor_acquire()
{
    std::unique_lock<std::mutex> lock(guard);
    int compositor;

    while (nready <= 0)
        cond.wait(lock);

    compositor = first_ready;
    first_ready = (first_ready + 1) % nbuffers;
    nready--;
    ncompositors++;

    return ring[compositor];
}

void mc::SwitchingBundle::compositor_release(std::shared_ptr<mg::Buffer> const& released_buffer)
{
    std::unique_lock<std::mutex> lock(guard);
    assert(ring[first_compositor] == released_buffer);
    first_compositor = (first_compositor + 1) % nbuffers;
    ncompositors--;
    cond.notify_all();
}

void mc::SwitchingBundle::force_requests_to_complete()
{
    std::unique_lock<std::mutex> lock(guard);
    drop_frames(nready);
    cond.notify_all();
}

void mc::SwitchingBundle::allow_framedropping(bool allow_dropping)
{
    framedropping = allow_dropping;
}

mc::BufferProperties mc::SwitchingBundle::properties() const
{
    return bundle_properties;
}
