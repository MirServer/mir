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

#include "miroil/set_compositor.h"
#include "miroil/compositor.h"
#include <stdexcept>

// mir
#include <mir/server.h>
#include <mir/shell/shell.h>
#include <mir/compositor/compositor.h>

namespace miroil {

struct SetCompositor::CompositorImpl : public mir::compositor::Compositor
{
    CompositorImpl(const std::shared_ptr<miroil::Compositor> & compositor);
    
    auto get_wrapped() -> std::shared_ptr<miroil::Compositor>;    
    void start();
    void stop();
    
    std::shared_ptr<miroil::Compositor> custom_compositor;
};

SetCompositor::CompositorImpl::CompositorImpl(const std::shared_ptr<miroil::Compositor> & compositor) 
: custom_compositor(compositor)
{
}
    
auto SetCompositor::CompositorImpl::get_wrapped() 
-> std::shared_ptr<miroil::Compositor>
{ 
    return custom_compositor;     
}


void SetCompositor::CompositorImpl::start()
{
    return custom_compositor->start();
}

void SetCompositor::CompositorImpl::stop()
{
    return custom_compositor->stop();
}

SetCompositor::SetCompositor(ConstructorFunction constr, InitFunction init)
    : constructor_function(constr), init_function(init)
{
}

void SetCompositor::operator()(mir::Server& server)
{
    server.override_the_compositor([this]
    {
        auto result = std::make_shared<CompositorImpl>(constructor_function());
        compositor_impl = result;
        return result;
    });

    server.add_init_callback([&, this]
        {
            if (auto const comp = compositor_impl.lock())
            {
                init_function(server.the_display(), comp->get_wrapped(), server.the_shell());
            }
            else
            {
                throw std::logic_error("No m_compositor available. Server not running?");
            }
        });
}

}
